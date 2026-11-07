#!/bin/sh
# tools/from-rogue.sh's table against ZyperOS, and the tool against a ROGUE
# card made up here.
#
#     make check-from-rogue
#
# THE TABLE. Each row that fills a shelf names a Roms/ folder systems.cfg has,
# and file types that shelf lists; its track files ("also": a .bin beside a
# .cue) are ones the shelf does not list, or each track would show as a game.
# The set rows' three shelves (Neo Geo, Arcade, MAME) are shelves. A skip
# row's system (PSP, by my decision) is not a shelf either. A pending
# row's folder is NOT a shelf yet: the day it is, the check fails, and the row
# becomes a game row. BGM and Music are the folders src/bgm.h and
# src/muselib.c read, in types they play. Every disc BIOS systems.cfg names
# comes over by a bios row, and every bios row serves folders the table has.
# Every folder that holds files on my ROGUE card (tools/fixtures/
# rogue-card.txt) has a row, so nothing on it is left out unless the table
# says why.
#
# THE TOOL, on a ROGUE card of a few dozen small files built under
# build-native/from-rogue-check, offline (the set lists are seeded in DL):
# a dry run writes nothing; a run puts every game, track, save and BIOS file
# where the table says, byte for byte and with ROGUE's time, and leaves the
# rest; the ROGUE side is the same, file for file, after it; a file already on
# the card is not touched and nothing is left as .part; a second run copies
# nothing; a card on ROGUE's own partition is refused; with no room nothing
# is written; a skipped folder never comes over, and a pending one only when
# named; --covers brings
# the art; and the favorites come out as ZyperOS keys them.
set -u

T=tools/from-rogue.sh
CFG=config/systems.cfg
FIX=tools/fixtures/rogue-card.txt
fails=0
fail() { echo "FAIL $*" >&2; fails=$((fails + 1)); }

# ---- the table
TABLE=$(sh "$T" --table) || { echo "FAIL $T --table" >&2; exit 1; }
SHELVES=$(awk -F'|' '$1 == "sys" { print $3 "|" $8 "|" $9 }' "$CFG")
shelf_exts() { printf '%s\n' "$SHELVES" | awk -F'|' -v f="$1" '$1 == f { gsub(/,/, " ", $2); print " " $2 " " }'; }
bgm_dir=$(sed -n 's/^#define BGM_DIR "\(.*\)"/\1/p' src/bgm.h)
music_types=$(awk '/static const char \*AUDIO\[\]/,/NULL/' src/muselib.c | tr -c 'a-z0-9\n' ' ')

mkdir -p build-native
printf '%s\n' "$TABLE" | grep -v '^#' > build-native/from-rogue-table.txt
while IFS='|' read -r kind rogue place types also; do
	folder=${place#Roms/}
	case $kind in
	game|keep|port)
		exts=$(shelf_exts "$folder")
		[ -n "$exts" ] || { fail "$rogue: $place is not a shelf in $CFG"; continue; }
		for t in $types; do
			case $exts in *" $t "*) ;; *) fail "$rogue: $place's shelf does not list .$t" ;; esac
		done
		for t in $also; do
			case $exts in *" $t "*) fail "$rogue: .$t is a track file, but $place's shelf lists it as a game" ;; esac
		done ;;
	sets)
		for f in "Neo Geo" Arcade MAME; do
			exts=$(shelf_exts "$f")
			[ -n "$exts" ] || { fail "$rogue: Roms/$f is not a shelf"; continue; }
			case $exts in *" zip "*) ;; *) fail "$rogue: Roms/$f does not list .zip" ;; esac
		done
		for f in "Neo Geo" Arcade; do
			case $(shelf_exts "$f") in *" 7z "*) ;; *) fail "$rogue: Roms/$f does not list .7z" ;; esac
		done ;;
	skip)
		printf '%s\n' "$SHELVES" | awk -F'|' -v f="$rogue" 'tolower($1) == tolower(f) { found = 1 } END { exit !found }' &&
			fail "$rogue is skipped by decision ($types), but Roms/$rogue is a shelf now" ;;
	pending)
		[ -z "$(shelf_exts "$folder")" ] ||
			fail "$rogue: $place is a shelf now; make its row a game row (types: $(shelf_exts "$folder"))" ;;
	bgm)
		[ "$place" = "Roms/$bgm_dir" ] || fail "$rogue: $place is not src/bgm.h's Roms/$bgm_dir"
		for t in $types; do case $t in mp3|ogg) ;; *) fail "$rogue: the menu music plays no .$t" ;; esac; done ;;
	music)
		grep -q '"%s/Music"' src/muselib.c || fail "$rogue: src/muselib.c reads no Music folder"
		for t in $types; do
			case " $music_types " in *" $t "*) ;; *) fail "$rogue: Muse plays no .$t" ;; esac
		done ;;
	bios)
		for r in $also; do
			printf '%s\n' "$TABLE" | grep -q "^[a-z]*|$r|" || fail "bios $rogue serves $r, which has no row"
		done ;;
	route)
		case $place in "Roms/Arcade"|"Roms/Neo Geo"|"Roms/MAME"|-) ;; *) fail "route $rogue: $place is no set shelf" ;; esac
		printf '%s\n' "$TABLE" | grep -q "^[a-z]*|${rogue%%/*}|" || fail "route $rogue: ${rogue%%/*} has no row"
		case $types in ''|*[!0-9]*) fail "route $rogue: no size" ;; esac ;;
	other) ;;
	*) fail "$rogue: unknown kind $kind" ;;
	esac
done < build-native/from-rogue-table.txt

# every disc BIOS a shelf checks for comes over, each of a shelf's names
for b in $(awk -F'|' '$1 == "sys" && $9 != "" { gsub(/,/, " ", $9); print $9 }' "$CFG"); do
	printf '%s\n' "$TABLE" | awk -F'|' -v b="Bios/$b" '
		$1 == "bios" && ($3 == b || (substr($3, length($3)) == "/" && index(b, $3) == 1)) { ok = 1 }
		END { exit !ok }' || fail "no bios row brings Bios/$b, which systems.cfg checks for"
done

# every folder with files on my card has a row
for f in $(awk -F'\t' '!/^#/ { print $1 }' "$FIX"); do
	printf '%s\n' "$TABLE" | grep -q "^[a-z]*|$f|" || fail "ROGUE's $f/ ($FIX) has no row in the table"
done

# ---- the tool
D=build-native/from-rogue-check
rm -rf "$D"
R=$D/ROMS/roms C=$D/CARD DL=$D/dl
mkdir -p "$R" "$C/Roms/NES" "$DL"
export DL
file() { # PATH TEXT - a file on the made-up ROGUE card
	mkdir -p "$(dirname "$R/$1")"
	printf '%s\n' "$2" > "$R/$1"
}
file "nes/A-M/Alpha (USA).nes" alpha
file "nes/Best NES Games/Alpha (USA).nes" alpha
file "nes/N-Z/Zeta (USA).nes" zeta
file "nes/A-M/images/Alpha (USA).png" alpha-art
file "nes/._Alpha (USA).nes" shadow
: > "$R/nes/Empty.nes"
file nes/gamelist.xml '<?xml version="1.0"?>
<gameList>
	<game>
		<path>./A-M/Alpha (USA).nes</path>
		<name>Alpha</name>
		<favorite>true</favorite>
	</game>
	<game><path>./N-Z/Zeta (USA).nes</path><name>Zeta</name></game>
</gameList>'
file "psx/Game (USA).cue" 'FILE "Game (USA).bin" BINARY'
file "psx/Game (USA).bin" game-track
file "psx/Game (USA).srm" old-card
file "saves/psx/PCSX-ReARMed/Game (USA).srm" new-card
touch -t 202001010000 "$R/psx/Game (USA).srm"
touch -t 202401010000 "$R/saves/psx/PCSX-ReARMed/Game (USA).srm"
file "gb/A-M/Pocket (USA).gb" pocket
file "saves/A-M/Gambatte/Pocket (USA).srm" pocket-save
file "saves/A-M/Gambatte/Lonely (USA).srm" no-game
file nds/Racer.nds racer
file nds/Racer.dsv racer-save
file fbneo/mslug.zip mslug
file fbneo/1941.zip 1941
file fbneo/hackset.zip hack
file fbneo/neogeo.zip neogeo-bios
file fbneo/fbneo/mslug.fs mslug-nvram
file arcade/fbneo/1941.hi 1941-scores
file varcade/asylum.zip asylum
file mame/pacman.zip pacman
file mame/mame2003-plus/hi/pacman.hi pacman-scores
file mame/mame2003/hi/pacman.hi old-core-scores
file naomi/cvs2.zip cvs2
file naomi/cvs2/gdl-0008.chd cvs2-disc
file naomi/cvs2.zip.nvmem cvs2-nvmem
file dreamcast/Game.chd dc-game
file dreamcast/data/vmu_save_A1.bin standalone-vmu
file bios/dc/vmu_save_A1.bin old-vmu
file bios/vmu_save_A1.bin new-vmu
touch -t 202001010000 "$R/bios/dc/vmu_save_A1.bin"
touch -t 202401010000 "$R/bios/vmu_save_A1.bin"
file bios/dc/dc_boot.bin dc-bios
file bios/scph5501.bin ps-bios
file bios/scph1000.bin unread-bios
file bios/scd_U.brm scd-ram
file "segacd/Sonic CD (USA).chd" sonic-cd
file ports/Game.sh '#!/bin/sh'
file ports/game/data.bin port-data
: > "$R/ports/game/.astc_done"
file ports/._Game.sh shadow
file ports/gamelist.xml '<gameList/>'
file ports/media/Game.png port-art
file ports/other/Other.sh '#!/bin/sh'
file BGM/song.mp3 song
file BGM/notes.txt notes
file music/Loose.mp3 loose
file music/Artist/Album/01.flac album-track
file pico-8/cart.p8 cart
file pico-8/image.p8.png image-cart
file pico-8/Splore.png picture
file pico-8/cdata/cart.p8d.txt cart-data
file psp/Game.iso psp-game
file saturn/Game.chd saturn-game
file bios/saturn_bios.bin saturn-bios
# a pending folder's game: the first pending row's, while the table has one
pend=$(printf '%s\n' "$TABLE" | awk -F'|' '$1 == "pending" { split($4, t, " "); print $2 "|" $3 "|" t[1]; exit }')
pend_dir=${pend%%|*} pend_place=$(printf '%s' "$pend" | cut -d'|' -f2) pend_ext=${pend##*|}
[ -z "$pend" ] || file "$pend_dir/Game.$pend_ext" pending-game
file savestates/gba/x.state.auto state
file mystery/x.bin mystery
# two of the table's routes, at their sizes, and a file of a routed name at
# another size, which gets the name rule
mkdir -p "$R/varcade" "$R/arcade"
dd if=/dev/zero of="$R/varcade/volfied.zip" bs=1 count=0 seek=547454 2> /dev/null
dd if=/dev/zero of="$R/arcade/wofch.zip" bs=1 count=0 seek=3806336 2> /dev/null
file varcade/kageki.zip not-the-routed-kageki
printf 'zeta-already-there\n' > "$C/Roms/NES/Zeta (USA).nes"
printf '#\nmslug\n' > "$DL/neogeo-sets.txt"
printf '#\nmslug\n1941\nvolfied\nwofch\nkageki\n' > "$DL/arcade-sets.txt"
printf '#\npacman\nasylum\n' > "$DL/mame-sets.txt"
for l in neogeo-sets arcade-sets mame-sets; do printf '#\nneogeo\n' > "$DL/$l.bios"; done

snap() { # DIR - every file and folder's path, mode, size and time, and each file's checksum
	( cd "$1" && find . -exec ls -lnd {} + | LC_ALL=C sort && find . -type f -exec cksum {} + | LC_ALL=C sort )
}
rogue_before=$(snap "$R")
card_before=$(snap "$C")
run() { ROGUE=$R sh "$T" "$@" > "$D/out.txt" 2>&1; }

run --dry-run "$C" || fail "the dry run failed: $(cat "$D/out.txt")"
[ "$(snap "$C")" = "$card_before" ] || fail "the dry run wrote on the card"
[ -z "$pend" ] || grep -q "$pend_dir -> $pend_place: TODO" "$D/out.txt" ||
	fail "the dry run does not name $pend_dir as a TODO"
grep -q "left: skipped by decision (no PSP on ZyperOS): 1" "$D/out.txt" || fail "the dry run does not name psp as skipped by decision"

run "$C" || fail "the run failed: $(cat "$D/out.txt")"
same() { # ROGUE-PATH CARD-PATH
	if [ ! -f "$C/$2" ]; then fail "not copied: $1 -> $2"; return; fi
	cmp -s "$R/$1" "$C/$2" || fail "$2 is not $1 byte for byte"
	{ [ "$R/$1" -nt "$C/$2" ] || [ "$C/$2" -nt "$R/$1" ]; } && fail "$2 does not keep $1's time"
}
gone() { [ ! -e "$C/$1" ] || fail "copied, and should not be: $1"; }
same "nes/A-M/Alpha (USA).nes"            "Roms/NES/Alpha (USA).nes"
same "psx/Game (USA).cue"                 "Roms/PlayStation/Game (USA).cue"
same "psx/Game (USA).bin"                 "Roms/PlayStation/Game (USA).bin"
same "saves/psx/PCSX-ReARMed/Game (USA).srm" "Saves/Game (USA).srm"
same "gb/A-M/Pocket (USA).gb"             "Roms/Game Boy/Pocket (USA).gb"
same "saves/A-M/Gambatte/Pocket (USA).srm" "Saves/Pocket (USA).srm"
same nds/Racer.dsv                        Saves/Racer.dsv
same fbneo/mslug.zip                      "Roms/Neo Geo/mslug.zip"
same fbneo/1941.zip                       Roms/Arcade/1941.zip
same varcade/asylum.zip                   Roms/MAME/asylum.zip
same fbneo/fbneo/mslug.fs                 Saves/fbneo/mslug.fs
same arcade/fbneo/1941.hi                 Saves/fbneo/1941.hi
same mame/pacman.zip                      Roms/MAME/pacman.zip
same varcade/volfied.zip                  Roms/MAME/volfied.zip
same varcade/kageki.zip                   Roms/Arcade/kageki.zip
gone Roms/Arcade/volfied.zip; gone Roms/Arcade/wofch.zip; gone Roms/MAME/wofch.zip
same mame/mame2003-plus/hi/pacman.hi      Saves/mame2003-plus/hi/pacman.hi
same naomi/cvs2.zip                       Roms/NAOMI/cvs2.zip
same naomi/cvs2/gdl-0008.chd              Roms/NAOMI/cvs2/gdl-0008.chd
same naomi/cvs2.zip.nvmem                 Saves/reicast/cvs2.zip.nvmem
same dreamcast/data/vmu_save_A1.bin       Saves/flycast/vmu_save_A1.bin
same bios/vmu_save_A1.bin                 Bios/dc/vmu_save_A1.bin
same bios/dc/dc_boot.bin                  Bios/dc/dc_boot.bin
same bios/scph5501.bin                    Bios/scph5501.bin
same bios/scd_U.brm                       "Saves/Sonic CD (USA).srm"
same ports/Game.sh                        Roms/Ports/Game.sh
same ports/game/data.bin                  Roms/Ports/game/data.bin
same ports/game/.astc_done                Roms/Ports/game/.astc_done
same BGM/song.mp3                         Roms/BGM/song.mp3
same music/Loose.mp3                      Music/Singles/Loose.mp3
same music/Artist/Album/01.flac           Music/Artist/Album/01.flac
same pico-8/cart.p8                       Roms/PICO-8/cart.p8
same pico-8/image.p8.png                  Roms/PICO-8/image.p8.png
same pico-8/cdata/cart.p8d.txt            Saves/cdata/cart.p8d.txt
same saturn/Game.chd                      Roms/Saturn/Game.chd
same bios/saturn_bios.bin                 Bios/saturn_bios.bin
gone "Roms/NES/Empty.nes"; gone "Roms/NES/._Alpha (USA).nes"; gone "Roms/NES/.media"
gone "Saves/Lonely (USA).srm"; gone Roms/Arcade/hackset.zip; gone Roms/Arcade/neogeo.zip
gone "Roms/Neo Geo/neogeo.zip"; gone Saves/mame2003-plus/hi/pacman.hi.part
gone Bios/scph1000.bin; gone Roms/Ports/._Game.sh; gone Roms/Ports/gamelist.xml
gone Roms/Ports/media; gone Roms/BGM/notes.txt; gone Roms/PICO-8/Splore.png; gone Roms/PSP
[ -z "$pend" ] || gone "$pend_place"
gone Roms/mystery
[ "$(cat "$C/Roms/NES/Zeta (USA).nes")" = zeta-already-there ] || fail "a file already on the card was written over"
[ -z "$(find "$C" -name '*.part')" ] || fail ".part files left: $(find "$C" -name '*.part')"
[ "$(snap "$R")" = "$rogue_before" ] || fail "the ROGUE side changed"
grep -q "a port with its script only inside its folder, so not on the shelf: other/Other.sh" "$D/out.txt" ||
	fail "ports/other/Other.sh is not reported as off the shelf"
grep -q "duplicate: the same file in another ROGUE subfolder: 1" "$D/out.txt" || fail "the NES duplicate is not reported"
grep -q "no driver for it in FinalBurn Neo" "$D/out.txt" || fail "the hack set is not reported"
grep -q "a save for no game coming over: 1" "$D/out.txt" || fail "the save with no game is not reported"
printf 'nes\tfav.NES\tAlpha (USA).nes\n' | cmp -s - "$DL/favorites.tsv" ||
	fail "favorites.tsv is not the NES favorite as ZyperOS keys it: $(cat "$DL/favorites.tsv")"

card_after=$(snap "$C")
run "$C" || fail "the second run failed"
grep -q "^copied 0 (0 MB)" "$D/out.txt" || fail "a second run copied again: $(tail -1 "$D/out.txt")"
[ "$(snap "$C")" = "$card_after" ] || fail "a second run changed the card"

if ROGUE=$R sh "$T" "$D/ROMS" > "$D/out.txt" 2>&1; then fail "a card on ROGUE's own partition was not refused"; fi
[ "$(snap "$R")" = "$rogue_before" ] || fail "the refused run changed the ROGUE side"

rm -rf "$C" && mkdir -p "$C/Roms"
env RESERVE_MB=999999999 ROGUE="$R" sh "$T" "$C" nes > "$D/out.txt" 2>&1 || fail "the run with no room failed"
[ -z "$(find "$C" -type f)" ] || fail "with no room, files were written: $(find "$C" -type f | head -3)"
grep -q "NO ROOM" "$D/out.txt" || fail "with no room, the report does not say so"

if run "$C" psp; then fail "psp, skipped by decision, could be named"; fi
run --covers "$C" nes $pend_dir || fail "the run with --covers and ${pend_dir:-no pending folder} failed"
same "nes/A-M/images/Alpha (USA).png" "Roms/NES/.media/Alpha (USA).png"
[ -z "$pend" ] || same "$pend_dir/Game.$pend_ext" "$pend_place/Game.$pend_ext"
gone Roms/PSP

if [ "$fails" -ne 0 ]; then echo "$fails failed" >&2; exit 1; fi
echo "ok: from-rogue's table matches systems.cfg and my card, and the tool copies as it says"
