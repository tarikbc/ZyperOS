#!/bin/sh
# Move a ROGUE OS card's library onto a ZyperOS card: every game, BIOS and
# battery save ZyperOS has a place for, and a report of what it has none for.
#
#   tools/from-rogue.sh [--dry-run] [--covers] [-v] CARD [FOLDER ...]
#   tools/from-rogue.sh --table
#
# CARD is the ZyperOS card's root, the folder holding Roms/, Bios/ and Saves/
# (/Volumes/ZYPEROS, say). The ROGUE side is ROGUE=/Volumes/ROMS/roms unless
# set, the roms/ folder of ROGUE's games partition: the card's, or, once
# ZyperOS is on that card, its image's - `hdiutil attach -readonly
# rogue-card.img` mounts the image's ROMS there. FOLDER is a ROGUE folder
# from the table below; with none, every one that has a shelf. Name them on a
# card with little room: `--dry-run CARD psx` says how big before anything is
# written.
#
# OR NO TOOL AT ALL. ZyperOS reads ROGUE's folders as they are (DESIGN.md
# 11.40): roms/nds copied to Roms/nds shows on the Nintendo DS shelf, with
# ROGUE's images/, videos/ and gamelist.xml for its covers, names, details
# and favorites. config/systems.cfg's es lines name each ROGUE folder this
# table copies (tools/config-check.c holds the two together). This tool is
# for a card in ZyperOS's own folders, with the saves, BIOS files and the
# arcade checks below.
#
# THE RULES. Nothing on the ROGUE side is ever written: the tool reads it, and
# refuses a CARD on ROGUE's own partition (on FAT, CARD/Roms would be ROGUE's
# roms/). Nothing on the ZyperOS side is ever overwritten: a file already
# there is left as it is and counted, so a second run copies only what is new.
# Each file goes in through a .part, renamed once whole, so a pulled card
# never leaves half a game under a name the shelf lists, and keeps ROGUE's
# modification time, which the shelf's newest-first order reads. Before each
# file the card must keep RESERVE_MB (64) free after it, or the file is
# skipped and counted as no room. --dry-run writes nothing on either card,
# checks no space per file, and ends with what the run needs against what the
# card has free.
#
# HOW. One listing of the ROGUE card (find and stat, read only), then one awk
# pass over it that plans every copy from the table, then the copies. The
# plan, what is left behind and why, and what each copy did stay in DL
# (/tmp/zyperos-from-rogue) as tab-separated files: plan.tsv, left.tsv,
# done.tsv and favorites.tsv.
#
# THE TABLE (--table prints it; tools/from-rogue-check.sh holds it against
# config/systems.cfg and against the folders on my card):
#
#   game     the folder's files of the listed types, from any subfolder, into
#            one flat shelf folder: ROGUE splits NES, SNES, Genesis, Game Boy
#            and Color into A-M/ and N-Z/, and ZyperOS lists only a shelf's top
#            (src/library.c). Two files of one name: the first in path order
#            is kept, the second left and named, as a duplicate when it is the
#            same size.
#            The last field, "also", is track files copied beside their .cue
#            but not listed by the shelf (a PlayStation .bin, a PC Engine
#            CD's .cd).
#   keep     the same, but subfolders kept: a NAOMI GD-ROM game is its set's
#            zip and a folder of that name holding its CHD, where Flycast
#            looks (the shelf hides the folder behind the zip of its name).
#   sets     ROM sets, sorted by who has a driver for them: FinalBurn Neo's
#            own Neo Geo list at the commit ZyperOS pins (mk/fetch-vendor.sh,
#            da14132) to Neo Geo, the rest of its arcade list to Arcade, else
#            MAME 2003-Plus's list at its pin (91f2f15) to MAME, else left
#            behind and named: ROGUE's fbneo/ holds hacks no FinalBurn Neo of
#            ours knows. The lists come from GitHub once into DL, and the
#            pins and the three URLs below change together.
#   route    one of my set files, known by its size, sent to the other
#            core's shelf or left behind, where its ROMs say otherwise than
#            its name: from a check of every set's ROMs (the rows say how).
#   port     PortMaster's layout as it is, into Roms/Ports: the scripts, their
#            folders and PortMaster/ itself (launch.sh puts ZyperOS's
#            control.txt over its own before every port). Dot files and empty
#            files too - a port's .astc_done is how it knows its first-run
#            conversion is done - but not macOS's ._ shadows. A port whose
#            script is only inside its folder is named: the shelf lists the
#            scripts at the top.
#   bgm      ROGUE's menu music, into Menu Music, the folder ZyperOS plays
#            its menu music from (src/bgm.h); Roms/BGM is not read since 1.0.5.
#   music    ROGUE's music folder into Muse's Music/; a song loose at its top
#            into Music/Singles, where the launcher would move it anyway.
#   skip     a folder left behind by decision, all of it, with the reason.
#            The check fails the day such a folder's system becomes a shelf.
#            None now: the PSP was one until 2026-11-12, when it became a
#            shelf.
#   pending  a system ZyperOS has no shelf for yet. Left out unless named, and
#            then copied into the folder its shelf is expected to use; the
#            check fails the day that folder becomes a shelf, so the row
#            becomes a game row then.
#   bios     a BIOS file ZyperOS's cores read, from ROGUE's bios/ (its
#            RetroArch system folder, which Bios/ is for Diatom): a file, a
#            folder/ with everything under it, or a pattern with one *. The
#            last field is the ROGUE folders whose games need it. bios/ also
#            holds firmware for systems ZyperOS does not run, left and counted.
#   other    no shelf: saves/ and the saves beside the games (below),
#            savestates/ (left: ZyperOS's cores are not ROGUE's, and a state
#            is only good for the core and version that wrote it), and the
#            rest.
#
# SAVES. A battery save is a .srm named after its ROM, flat in Saves/
# (diatom/src/save.c), from beside the games or from saves/<folder>/<core>/,
# where RetroArch kept them; for each ROM the newest one wins, and a .srm
# whose name matches no game coming over is left and named. Also:
#   nds/*.dsv              Saves/, DraStic's, which ZyperOS turns into the
#                          .srm DSperate reads the first time (dsv_import)
#   FinalBurn Neo's .nv .hi .fs .memcard, from fbneo/ and fba/ folders and the
#                          arcade folders' tops: Saves/fbneo/, where its
#                          libretro build keeps all four
#   MAME 2003-Plus's mame2003-plus/{cfg,hi,nvram,memcard}/
#                          Saves/mame2003-plus/<same>/
#   naomi/<set>.zip.eeprom, .nvmem, .nvmem2, and Flycast's reicast/ folder
#                          Saves/reicast/
#   bios/dc/vmu_save_*.bin and bios/vmu_save_*.bin, the newer of the two
#                          Bios/dc/, flycast2021's VMUs, where ROGUE's were
#   dreamcast/data/vmu_save_*.bin, dc_nvmem.bin
#                          Saves/flycast/, the standalone Flycast's (launch.sh)
#   pico-8/cdata/          Saves/cdata/, fake08's cart data
#   psp/PSP/SAVEDATA/      Saves/PSP/SAVEDATA/, the PSP's own saves, where
#                          ROGUE's standalone PPSSPP kept its memory stick
#                          and ZyperOS's PPSSPP core keeps it in Saves/
#   3do/opera/             Saves/opera/, Opera's per-game NVRAM
#   bios/scd_U.brm, _E, _J Saves/<disc>.srm, for each Sega CD disc of that
#                          region: ROGUE ran the Sega CD on genesis_plus_gx,
#                          whose one backup RAM per region is the 8 KB image
#                          PicoDrive keeps per disc. ROGUE's own segacd/*.srm
#                          are 8 KB from 2017, formatted and all but empty.
# Favorites (<favorite> in ROGUE's gamelist.xml files) are listed in
# favorites.tsv as ZyperOS keys them (src/favorites.h), not written: they
# live in the card's library.db, which this tool does not touch.
#
# --covers also brings ROGUE's art, images/<game>.png, into each folder's
# .media/, where the shelf looks for covers. Off by default: some of ROGUE's
# art is box art and some a screenshot montage, and tools/scrape-art.py
# fetches libretro's box art for most systems instead. It is the only source
# for PICO-8, which libretro has no art for.
set -eu

ROGUE=${ROGUE:-/Volumes/ROMS/roms}
DL=${DL:-/tmp/zyperos-from-rogue}
RESERVE_MB=${RESERVE_MB:-64}
# FinalBurn Neo's Neo Geo and arcade lists at the commit fetch-vendor.sh pins
# (da14132), and MAME 2003-Plus's at its pin (91f2f15)
FBNEO_DATS="https://raw.githubusercontent.com/libretro/FBNeo/da14132/dats"
NEOGEO_DAT="$FBNEO_DATS/FinalBurn%20Neo%20(ClrMame%20Pro%20XML,%20Neogeo%20only).dat"
ARCADE_DAT="$FBNEO_DATS/FinalBurn%20Neo%20(ClrMame%20Pro%20XML,%20Arcade%20only).dat"
MAME_XML="https://raw.githubusercontent.com/libretro/mame2003-plus-libretro/91f2f15/metadata/mame2003-plus.xml"
TAB=$(printf '\t')

table() {
	cat <<'EOF'
# kind|ROGUE folder or bios file|ZyperOS place|file types|also, or for bios the folders it serves
game|atari2600|Roms/Atari 2600|a26 bin zip|
game|gameandwatch|Roms/Game & Watch|mgw|
game|atari5200|Roms/Atari 5200|a52 bin zip|
game|vectrex|Roms/Vectrex|vec bin zip|
game|nes|Roms/NES|nes fds unf unif zip|
game|msx2|Roms/MSX2|rom mx1 mx2 dsk cas zip|
game|atari7800|Roms/Atari 7800|a78 bin zip|
game|mastersystem|Roms/Master System|sms zip|
game|gb|Roms/Game Boy|gb dmg zip|
game|megadrive|Roms/Genesis|md gen bin smd zip|
game|pcengine|Roms/TurboGrafx-16|pce sgx cue ccd chd toc m3u zip|bin img iso wav sub
game|pcenginecd|Roms/TurboGrafx-16|cue ccd chd toc m3u|cd bin img iso wav sub
game|atarilynx|Roms/Atari Lynx|lnx o zip|
game|gamegear|Roms/Game Gear|gg zip|
game|sfc|Roms/SNES|sfc smc zip|
game|segacd|Roms/Sega CD|chd cue iso m3u|bin wav
game|3do|Roms/3DO|iso chd cue|bin
game|sega32x|Roms/32X|32x zip|
game|saturn|Roms/Saturn|chd cue iso ccd mds|bin img
game|virtualboy|Roms/Virtual Boy|vb vboy zip|
game|psx|Roms/PlayStation|cue chd pbp m3u ccd toc iso mdf cbn exe|bin img sub
game|neocd|Roms/Neo Geo CD|chd cue|bin img wav
game|n64|Roms/Nintendo 64|n64 z64 v64 zip|
game|ngp|Roms/Neo Geo Pocket|ngp ngc ngpc npc zip|
game|gbc|Roms/Game Boy Color|gbc cgb zip|
game|wonderswan|Roms/WonderSwan|ws wsc zip|
game|ngpc|Roms/Neo Geo Pocket Color|ngp ngc ngpc npc zip|
game|dreamcast|Roms/Dreamcast|chd cdi gdi cue|bin raw
game|wonderswancolor|Roms/WonderSwan Color|ws wsc zip|
game|j2me|Roms/J2ME|jar kjx|
game|gba|Roms/Game Boy Advance|gba agb zip|
game|pokemini|Roms/Pokemon Mini|min zip|
game|openbor|Roms/OpenBOR|pak|
game|nds|Roms/Nintendo DS|nds zip|
game|pico-8|Roms/PICO-8|p8 png|
game|psp|Roms/PSP|iso cso chd pbp|
game|coleco|Roms/ColecoVision|col cv bin rom zip|
game|tic-80|Roms/TIC-80|tic png|
sets|arcade|Roms/Arcade|zip 7z|
sets|neogeo|Roms/Neo Geo|zip 7z|
sets|fbneo|Roms/Arcade|zip 7z|
sets|varcade|Roms/Arcade|zip 7z|
sets|hbmame|Roms/Arcade|zip 7z|
game|mame|Roms/MAME|zip|
keep|naomi|Roms/NAOMI|zip 7z|chd
keep|atomiswave|Roms/Atomiswave|zip 7z|chd
port|ports|Roms/Ports|sh|
bgm|BGM|Menu Music|mp3 ogg|
music|music|Music|mp3 flac m4a m4b aac ogg oga opus wav|
# bios|ROGUE bios/ path|Bios/ path||the ROGUE folders it serves. Only what a
# ZyperOS core reads with ZyperOS's settings: pcsx_rearmed 7b0e8f8 looks for
# these six PlayStation BIOS names, by the disc's region; Opera's is the 3DO
# shelf's disc bios. Left in bios/, and counted: the Game Boy, Color and
# Advance boot ROMs, which mGBA would run, logo and all, before every game
# (mgba_use_bios is on and mgba_skip_bios off by default, and src/db.c sets
# neither), Genesis Plus GX's, which it reads only with its BIOS option on,
# and the firmware of systems ZyperOS has no core for.
bios|5200.rom|Bios/5200.rom||atari5200
bios|7800 BIOS (U).rom|Bios/7800 BIOS (U).rom||atari7800
bios|7800 BIOS (E).rom|Bios/7800 BIOS (E).rom||atari7800
bios|disksys.rom|Bios/disksys.rom||nes
bios|lynxboot.img|Bios/lynxboot.img||atarilynx
bios|syscard3.pce|Bios/syscard3.pce||pcengine pcenginecd
bios|bios_CD_U.bin|Bios/bios_CD_U.bin||segacd
bios|bios_CD_E.bin|Bios/bios_CD_E.bin||segacd
bios|bios_CD_J.bin|Bios/bios_CD_J.bin||segacd
bios|panafz10.bin|Bios/panafz10.bin||3do
bios|saturn_bios.bin|Bios/saturn_bios.bin||saturn
bios|sega_101.bin|Bios/sega_101.bin||saturn
bios|mpr-17933.bin|Bios/mpr-17933.bin||saturn
bios|scph5500.bin|Bios/scph5500.bin||psx
bios|scph5502.bin|Bios/scph5502.bin||psx
bios|scph101.bin|Bios/scph101.bin||psx
bios|scph7001.bin|Bios/scph7001.bin||psx
bios|scph5501.bin|Bios/scph5501.bin||psx
bios|scph1001.bin|Bios/scph1001.bin||psx
bios|neocd/|Bios/neocd/||neocd
bios|neogeo.zip|Bios/neogeo.zip||arcade neogeo fbneo varcade hbmame
bios|fbneo/hiscore.dat|Bios/fbneo/hiscore.dat||arcade neogeo fbneo varcade hbmame
bios|fbneo/*.zip|Bios/fbneo/||arcade neogeo fbneo varcade hbmame
bios|Machines/|Bios/Machines/||msx2
bios|Databases/|Bios/Databases/||msx2
bios|mame2003-plus/|Bios/mame2003-plus/||mame varcade
bios|dc/|Bios/dc/||dreamcast naomi atomiswave
bios|vmu_save_*.bin|Bios/dc/||dreamcast
bios|bios7.bin|Bios/bios7.bin||nds
bios|bios9.bin|Bios/bios9.bin||nds
bios|firmware.bin|Bios/firmware.bin||nds
bios|bios.min|Bios/bios.min||pokemini
bios|coleco.rom|Bios/coleco.rom||coleco
bios|colecovision.rom|Bios/colecovision.rom||coleco
other|bios|Bios|||
other|saves|Saves|||
other|savestates||||
other|screenshots||||
other|wifi||||
# route|ROGUE set file|shelf, or - to leave it|its size: My card's sets
# whose ROMs say otherwise than their name. Every set coming over was checked
# on 2026-11-06, by the CRCs in its zip, against FinalBurn Neo da14132's ROM
# lists (its arcade dat, less what its drivers mark BRF_OPT or BRF_NODUMP:
# it loads a ROM by CRC alone, and a missing ROM it needs stops the game) and
# MAME 2003-Plus 91f2f15's (by name or CRC, with the parent set's). These load
# only on the other core, or are one of two copies of a set where only the
# other loads. Bound to the size, so a different file of the name gets the
# name rule.
route|arcade/avsp.zip|Roms/MAME|10447340|
route|arcade/progear.zip|Roms/MAME|13800476|
route|arcade/wofch.zip|-|3806336|
route|fbneo/avsp.zip|-|10447343|
route|mame/vigilant.zip|Roms/Arcade|443625|
route|varcade/1942.zip|-|105715|
route|varcade/1943.zip|-|304996|
route|varcade/acrobatm.zip|-|1711042|
route|varcade/arbalest.zip|-|2099726|
route|varcade/aso.zip|-|124173|
route|varcade/avengers.zip|-|358507|
route|varcade/chopper.zip|-|423522|
route|varcade/cyvern.zip|-|14419598|
route|varcade/dangar.zip|-|167173|
route|varcade/downtown.zip|-|1659074|
route|varcade/dsaber.zip|-|1979157|
route|varcade/f1gp.zip|Roms/MAME|3980194|
route|varcade/fghtatck.zip|-|1720023|
route|varcade/finalizr.zip|-|81115|
route|varcade/firetrap.zip|-|256114|
route|varcade/fshark.zip|-|284514|
route|varcade/galaga88.zip|-|629202|
route|varcade/gaplus.zip|-|55379|
route|varcade/gemini.zip|-|370683|
route|varcade/gondo.zip|-|407591|
route|varcade/gseeker.zip|-|5213472|
route|varcade/gunnail.zip|-|1915778|
route|varcade/hbarrel.zip|-|531842|
route|varcade/kageki.zip|Roms/MAME|624950|
route|varcade/ktiger.zip|-|389397|
route|varcade/lastmisn.zip|Roms/MAME|208085|
route|varcade/macross.zip|-|2128374|
route|varcade/macrossp.zip|-|16024586|
route|varcade/mx5000.zip|-|411630|
route|varcade/nebulray.zip|-|9996586|
route|varcade/phelios.zip|-|1519611|
route|varcade/raphero.zip|-|6457343|
route|varcade/ryujin.zip|-|1579800|
route|varcade/srdarwin.zip|Roms/MAME|191667|
route|varcade/tdragon.zip|Roms/MAME|1271491|
route|varcade/terracre.zip|-|132019|
route|varcade/turbofrc.zip|-|2234188|
route|varcade/vandyke.zip|-|1444185|
route|varcade/volfied.zip|Roms/MAME|547454|
route|varcade/wndrplnt.zip|Roms/MAME|408437|
EOF
}

DRY=0 COVERS=0 VERBOSE=0
while [ $# -gt 0 ]; do
	case $1 in
	--dry-run) DRY=1 ;;
	--covers)  COVERS=1 ;;
	-v)        VERBOSE=1 ;;
	--table)   table; exit 0 ;;
	-*) echo "from-rogue: unknown option $1" >&2; exit 2 ;;
	*) break ;;
	esac
	shift
done
[ $# -ge 1 ] || { echo "usage: $0 [--dry-run] [--covers] [-v] CARD [FOLDER ...]" >&2; exit 2; }
CARD=${1%/}
shift

# The folders that can be named: every row with a shelf, and the pending ones
ALL=$(table | awk -F'|' '$1 ~ /^(game|keep|sets|port|bgm|music)$/ { printf "%s ", $2 }')
PENDING=$(table | awk -F'|' '$1 == "pending" { printf "%s ", $2 }')
WANT=${*:-$ALL}
for w in $WANT; do
	case " $ALL $PENDING " in *" $w "*) ;; *)
		echo "from-rogue: no ROGUE folder $w in the table (one of: $ALL$PENDING)" >&2; exit 2 ;;
	esac
done
# want ROWS - is any of these folders wanted; WANTED_AS is the first that is
want() {
	for w in $1; do
		case " $WANT " in *" $w "*) WANTED_AS=$w; return 0 ;; esac
	done
	return 1
}

[ -d "$ROGUE" ] || { echo "from-rogue: no ROGUE roms folder at $ROGUE" >&2; exit 1; }
[ -d "$CARD" ]  || { echo "from-rogue: no card at $CARD" >&2; exit 1; }
# A card that has never had ZyperOS on it has no Roms/ yet, and a typo in CARD
# would otherwise grow a new tree somewhere unexpected. Refuse rather than guess.
[ -d "$CARD/Roms" ] || { echo "from-rogue: $CARD has no Roms/ - is it a ZyperOS card?" >&2; exit 1; }
# And never ROGUE's own partition: FAT ignores case, so ROGUE's roms/ passes
# the Roms/ test above, and the copy would be written over the games it reads.
rogue_root=$(cd "$ROGUE/.." && pwd -P)
card_real=$(cd "$CARD" && pwd -P)
case "$card_real/" in "$rogue_root/"*)
	echo "from-rogue: $CARD is on the ROGUE card ($rogue_root); nothing written" >&2; exit 1 ;;
esac
case "$rogue_root/" in "$card_real/"*)
	echo "from-rogue: the ROGUE folder is inside $CARD; nothing written" >&2; exit 1 ;;
esac

mkdir -p "$DL"
# macOS's cp carries extended attributes, which a FAT or exFAT card keeps as
# ._ files beside every game; -X leaves them behind where cp has it
CP=cp
: > "$DL/cp-x.src"
if cp -X "$DL/cp-x.src" "$DL/cp-x.dst" 2> /dev/null; then CP="cp -X"; fi
rm -f "$DL/cp-x.src" "$DL/cp-x.dst"

# ---- the listing: size, modification time and path of every file, read only
if stat -f %z / > /dev/null 2>&1; then
	STATOPT=-f STATFMT="%z$TAB%m$TAB%N"   # BSD, macOS
else
	STATOPT=-c STATFMT="%s$TAB%Y$TAB%n"   # GNU, busybox
fi
# Sorted by path, so where two files would become one, which comes first does
# not hang on the order the card's directories happen to be in
echo "#" > "$DL/rogue.tsv"
( cd "$ROGUE" && find . -type f -exec stat "$STATOPT" "$STATFMT" {} + ) |
	LC_ALL=C sort -t "$TAB" -k 3 >> "$DL/rogue.tsv"

# ---- the set lists, once
# The <game name=...> of a dat or of MAME's XML. A BIOS set is <game
# isbios="yes" name=...> in FinalBurn Neo's and <game runnable="no" name=...>
# in MAME's, so it is not among them; those go to NAME.bios.
sets() { # URL NAME
	[ -s "$DL/$2.txt" ] && [ -s "$DL/$2.bios" ] && return 0
	curl -sSfL "$1" -o "$DL/$2.dat" || { rm -f "$DL/$2.dat"; return 1; }
	{ echo "#"; sed -n 's/.*<game name="\([^"]*\)".*/\1/p' "$DL/$2.dat"; } > "$DL/$2.txt"
	{ echo "#"; sed -n -e 's/.*<game isbios="yes" name="\([^"]*\)".*/\1/p' \
	                   -e 's/.*<game runnable="no" name="\([^"]*\)".*/\1/p' "$DL/$2.dat"; } > "$DL/$2.bios"
	rm -f "$DL/$2.dat"
}
NOLISTS=0
if want "arcade neogeo fbneo varcade hbmame"; then
	if ! { sets "$NEOGEO_DAT" neogeo-sets && sets "$ARCADE_DAT" arcade-sets && sets "$MAME_XML" mame-sets; }; then
		echo "from-rogue: cannot fetch FinalBurn Neo's and MAME 2003-Plus's set lists; no arcade sets this time" >&2
		NOLISTS=1
	fi
fi
for l in neogeo-sets arcade-sets mame-sets; do
	[ -s "$DL/$l.txt" ] || echo "#" > "$DL/$l.txt"
	[ -s "$DL/$l.bios" ] || echo "#" > "$DL/$l.bios"
done
cat "$DL/neogeo-sets.bios" "$DL/arcade-sets.bios" "$DL/mame-sets.bios" > "$DL/bios-sets.txt"

# ---- the plan
#
# One pass over the listing. Every file is a game, a track file, a save, a
# BIOS file, or left behind with a reason; plan.tsv gets row, kind, size,
# ROGUE path and card path for each copy, left.tsv row, reason, size and
# path for the rest. A battery save is matched to its game at the end, once
# every game is known, and where several files would become one save or BIOS
# file the newest wins.
PLAN_AWK=$(cat <<'AWK'
BEGIN {
	FS = "\t"; OFS = "\t"
	printf "" > planf; printf "" > leftf
	split("images image videos video media manuals downloaded_images snap marquee titles boxart wheel", a, " ")
	for (i in a) ART[a[i]] = 1
	# where ROGUE's cores kept their saves inside a games folder
	split("fbneo fba mame mame2003 mame2003-plus mame2010 opera data cdata psp freej2me", a, " ")
	for (i in a) DATA[a[i]] = 1
	# whose games a save in saves/<x>/<core>/ is for, by the core's folder
	CORE["gambatte"] = "gb gbc"; CORE["mgba"] = "gba gb gbc"; CORE["gpsp"] = "gba"
	CORE["snes9x"] = "sfc"; CORE["fceumm"] = "nes"; CORE["nestopia"] = "nes"
	CORE["genesis plus gx"] = "megadrive mastersystem gamegear"
	CORE["picodrive"] = "sega32x megadrive"
	CORE["mupen64plus-next"] = "n64"; CORE["parallel n64"] = "n64"
	CORE["pcsx-rearmed"] = "psx"; CORE["beetle wonderswan"] = "wonderswan wonderswancolor"
	CORE["beetle pce fast"] = "pcengine pcenginecd"; CORE["neocd"] = "neocd"
	CORE["game & watch"] = "gameandwatch"; CORE["handy"] = "atarilynx"
	CORE["beetle vb"] = "virtualboy"; CORE["prosystem"] = "atari7800"
	SETROWS = "arcade neogeo fbneo varcade hbmame"
}
FNR == 1 { file_no++ }
file_no == 1 {                                   # the table
	if ($0 ~ /^#/ || $0 == "") next
	split($0, f, "|")
	if (f[1] == "bios") { nb++; bsrc[nb] = f[2]; bdst[nb] = f[3]; brow[nb] = f[5]; next }
	if (f[1] == "route") { ROUTE[f[2]] = f[3]; ROUTESIZE[f[2]] = f[4]; next }
	kind[f[2]] = f[1]; place[f[2]] = f[3]
	types[f[2]] = " " f[4] " "; also[f[2]] = " " f[5] " "
	next
}
file_no == 2 { NG[$0] = 1; next }
file_no == 3 { AR[$0] = 1; next }
file_no == 4 { MA[$0] = 1; next }
file_no == 5 { BS[$0] = 1; next }
$0 == "#" { next }
{
	size = $1; mt = $2; rel = $3
	sub(/^\.\//, "", rel)
	n = split(rel, c, "/")
	if (n < 2) next                       # loose in roms/ itself
	top = c[1]; base = c[n]
	ext = ""; stem = base
	p = lastdot(base)
	if (p > 1) { ext = tolower(substr(base, p + 1)); stem = substr(base, 1, p - 1) }
	if (!(top in kind)) { left(top, "no row in the table for this ROGUE folder", size, rel); next }
	k = kind[top]
	if (k == "skip") { left(top, "skipped by decision (" substr(types[top], 2, length(types[top]) - 2) ")", size, rel); next }
	if (k == "port") { port(); next }
	if (base ~ /^\./) { left(top, "dot file", size, rel); next }
	if (size == 0) { left(top, "empty file", size, rel); next }
	sub_kind = ""
	for (i = 2; i < n; i++) {
		d = tolower(c[i])
		if (d in ART) { sub_kind = "art"; break }
		if (d in DATA) { sub_kind = "data"; break }
	}
	if (sub_kind == "art") {
		if (ext == "png") ARTF[top SUBSEP tolower(stem)] = rel
		left(top, "art and videos for ROGUE's menus", size, rel)
		next
	}
	if (top == "bios") { bios(); next }
	if (k == "other") { save(); next }
	if (sub_kind == "" && k != "sets" && ext != "") {
		if (index(types[top], " " ext " ")) {
			if (top == "pico-8" && ext == "png" && tolower(base) !~ /\.p8\.png$/)
				left(top, "a picture, not a cart", size, rel)
			else
				game(top, "game", dest_of(top))
			next
		}
		if (index(also[top], " " ext " ")) { game(top, "track", dest_of(top)); next }
	}
	if (k == "sets" && n == 2 && ext != "" && index(types[top], " " ext " ")) { set(); next }
	save()
}
function lastdot(s,    i) { for (i = length(s); i > 0; i--) if (substr(s, i, 1) == ".") return i; return 0 }
function first(r,    w) { split(r, w, " "); return w[1] }
function left(r, why, sz, path) { print first(r), why, sz, path > leftf }
# the path under the ROGUE folder
function under(    i, s) { s = c[2]; for (i = 3; i <= n; i++) s = s "/" c[i]; return s }
function dirname(s,    i) { for (i = length(s); i > 0; i--) if (substr(s, i, 1) == "/") return substr(s, 1, i - 1); return "." }
function dest_of(r) {
	if (kind[r] == "keep") return place[r] "/" under()
	if (kind[r] == "music") return (n == 2 ? "Music/Singles/" base : "Music/" under())
	return place[r] "/" base
}
# One game, track or set: the first of a name wins
function game(r, what, dest,    key) {
	if ((rel in ROUTE) && ROUTESIZE[rel] == size) {
		if (ROUTE[rel] == "-") { left(r, "another copy of this set loads, and this one does not (ROM check)", size, rel); return }
		dest = ROUTE[rel] "/" base
	}
	key = tolower(dest)
	if (key in taken) {
		if (takensize[key] == size)
			left(r, "duplicate: the same file in another ROGUE subfolder", size, rel)
		else
			left(r, "another file of the same name came first", size, rel)
		return
	}
	taken[key] = rel; takensize[key] = size
	print r, what, size, rel, dest > planf
	if (what == "game" && (kind[r] == "game" || kind[r] == "keep" || kind[r] == "sets")) {
		if (!((r SUBSEP tolower(stem)) in STEM)) STEM[r SUBSEP tolower(stem)] = stem
		if (!(tolower(stem) in ANYSTEM)) ANYSTEM[tolower(stem)] = r SUBSEP stem
		ng_++; g_r[ng_] = r; g_top[ng_] = top; g_stem[ng_] = stem; g_dest[ng_] = dest
	}
}
function set(    to) {
	if (nolists) { left(top, "set lists not fetched", size, rel); return }
	if (stem in NG) to = "Roms/Neo Geo"
	else if (stem in AR) to = "Roms/Arcade"
	else if (stem in MA) {
		if (ext != "zip") { left(top, "a MAME 2003-Plus set as ." ext ", which it does not read", size, rel); return }
		to = "Roms/MAME"
	}
	else if (stem in BS) { left(top, "BIOS set: Bios/ gets ROGUE's own from bios/", size, rel); return }
	else { left(top, "no driver for it in FinalBurn Neo da14132 or MAME 2003-Plus 91f2f15", size, rel); return }
	game(top, "game", to "/" base)
}
function port() {
	if (base ~ /^\._/ || base == ".DS_Store") { left(top, "macOS's own file", size, rel); return }
	if (n == 2 && tolower(base) == "gamelist.xml") { left(top, "ROGUE's game list", size, rel); return }
	if (tolower(c[2]) == "media") { left(top, "art and videos for ROGUE's menus", size, rel); return }
	game(top, (n == 2 && ext == "sh") ? "game" : "track", place[top] "/" under())
	if (n == 3 && ext == "sh" && c[2] != "PortMaster") inner[c[2]] = c[3]
	if (n == 2 && ext == "sh") topsh[tolower(stem)] = 1
}
# Newest wins, where several files would become one save or BIOS file
function nw(r, what, dest,    key) {
	key = tolower(dest)
	if (key in nwline) {
		if (mt + 0 > nwmt[key] + 0) {
			left(nwrow[key], "an older copy: the same save or BIOS file is newer elsewhere", nwsize[key], nwrel[key])
		} else {
			left(r, "an older copy: the same save or BIOS file is newer elsewhere", size, rel)
			return
		}
	}
	nwline[key] = first(r) OFS what OFS size OFS rel OFS dest
	nwmt[key] = mt; nwrow[key] = r; nwsize[key] = size; nwrel[key] = rel
}
function bios(    b, i, pre, suf, star, what) {
	b = substr(rel, 6)
	what = (base ~ /^vmu_save_/ || ext == "srm") ? "save" : "bios"
	for (i = 1; i <= nb; i++) {
		if (bsrc[i] == b) { nw(brow[i], what, bdst[i]); return }
		if (bsrc[i] ~ /\/$/ && index(b, bsrc[i]) == 1) {
			nw(brow[i], what, bdst[i] substr(b, length(bsrc[i]) + 1)); return
		}
		star = index(bsrc[i], "*")
		if (star) {
			pre = substr(bsrc[i], 1, star - 1); suf = substr(bsrc[i], star + 1)
			if (index(b, pre) == 1 && length(b) >= length(pre) + length(suf) &&
			    substr(b, length(b) - length(suf) + 1) == suf &&
			    index(substr(b, length(pre) + 1), "/") == 0) {
				nw(brow[i], what, bdst[i] base); return
			}
		}
	}
	if (b ~ /^scd_[UEJ]\.brm$/) { left("segacd", "Sega CD backup RAM: each disc's .srm, below", size, rel); return }
	if (ext == "sav" || b == "cart.brm") { left("bios", "a save in bios/, where no ZyperOS core reads one", size, rel); return }
	# FB Alpha's high scores, which FinalBurn Neo reads from its save folder
	if (b ~ /^fba\/[^\/]*\.hi$/) { nw(SETROWS, "save", "Saves/fbneo/" base); return }
	left("bios", "firmware no ZyperOS core reads: " (n > 2 ? c[2] "/" : "loose in bios/"), size, rel)
}
# Everything else: the save rules, in order
function save(    i, d, sub2, rows) {
	if (top == "savestates") { left(top, "save state: ZyperOS's cores are not ROGUE's", size, rel); return }
	if (top == "screenshots") { left(top, "screenshot", size, rel); return }
	if (top == "wifi") { left(top, "ROGUE's own SSH key", size, rel); return }
	if (kind[top] == "pending") { left(top, "pending: no shelf yet for " top, size, rel); return }
	for (i = 2; i < n; i++) {
		d = tolower(c[i])
		if (d == "mame2003-plus" && i < n - 1) {
			sub2 = tolower(c[i + 1])
			if (sub2 ~ /^(cfg|hi|nvram|memcard|diff)$/) {
				nw(kind[top] != "other" ? top : "mame varcade", "save", "Saves/mame2003-plus/" sub2 "/" base)
				return
			}
		}
		if (d == "mame2003" || d == "mame2010" || (d == "mame" && top != "saves")) {
			left(top, "a save of a MAME core ZyperOS does not run (" c[i] "/)", size, rel); return
		}
	}
	if (ext ~ /^(hi|nv|fs|memcard)$/ && (rel ~ /\/(fbneo|fba|FinalBurn Neo)\// || (n == 2 && index(" " SETROWS " ", " " top " ")))) {
		nw(index(" " SETROWS " mame ", " " top " ") ? top : SETROWS, "save", "Saves/fbneo/" base); return
	}
	if (ext ~ /^(eeprom|nvmem|nvmem2)$/ && (top == "naomi" || top == "atomiswave" || rel ~ /\/reicast\//)) {
		nw((top == "naomi" || top == "atomiswave") ? top : "naomi atomiswave", "save", "Saves/reicast/" base); return
	}
	if (top == "dreamcast" && tolower(c[2]) == "data") {
		if (base ~ /^vmu_save_.*\.bin$/ || base == "dc_nvmem.bin") { nw("dreamcast", "save", "Saves/flycast/" base); return }
		if (ext == "state") { left(top, "save state: ZyperOS's cores are not ROGUE's", size, rel); return }
	}
	if (top == "pico-8" && tolower(c[2]) == "cdata") { nw("pico-8", "save", "Saves/cdata/" base); return }
	if (top == "psp" && tolower(c[2]) == "psp" && n > 3 && tolower(c[3]) == "savedata") {
		d = "Saves/PSP/SAVEDATA"
		for (i = 4; i <= n; i++) d = d "/" c[i]
		nw("psp", "save", d); return
	}
	if (top == "3do" && tolower(c[2]) == "opera") { nw("3do", "save", "Saves/" under()); return }
	if (top == "segacd" && ext == "srm") {
		left(top, "ROGUE's own Sega CD .srm, 2017's and empty: scd_U.brm is what ROGUE wrote", size, rel); return
	}
	if (ext == "srm" || ext == "dsv") {
		rows = ""
		if (top == "saves") {
			if ((c[2] in kind) && kind[c[2]] != "other") rows = c[2]
			if (n > 3 && (tolower(c[n - 1]) in CORE)) rows = rows " " CORE[tolower(c[n - 1])]
		} else if (kind[top] != "other") rows = top
		ns++; s_rows[ns] = rows; s_size[ns] = size; s_mt[ns] = mt; s_rel[ns] = rel
		s_stem[ns] = stem; s_ext[ns] = ext; s_top[ns] = top
		return
	}
	if (ext == "ldci") { left(top, "RetroArch's disc index, not a save", size, rel); return }
	if (ext == "ngf") { left(top, "RACE's flash save; ZyperOS's Beetle NGP keeps its own", size, rel); return }
	if (ext == "xml" && n == 2) { left(top, "ROGUE's game list", size, rel); return }
	left(top, "other file (" (ext == "" ? "no extension" : "." ext) ")", size, rel)
}
END {
	# the battery saves, now that every game is known: the first folder the
	# save's place names with a game of its name, or any, where it names none
	for (i = 1; i <= ns; i++) {
		size = s_size[i]; mt = s_mt[i]; rel = s_rel[i]
		ls = tolower(s_stem[i]); got = ""
		m = split(s_rows[i], rr, " ")
		for (j = 1; j <= m; j++)
			if ((rr[j] SUBSEP ls) in STEM) { got = rr[j]; gs = STEM[rr[j] SUBSEP ls]; break }
		if (got == "" && m == 0 && (ls in ANYSTEM)) { split(ANYSTEM[ls], g2, SUBSEP); got = g2[1]; gs = g2[2] }
		if (got == "") { left(s_top[i], "a save for no game coming over", size, rel); continue }
		nw(got, "save", "Saves/" gs "." s_ext[i])
	}
	for (key in nwline) print nwline[key] > planf
	if (covers)
		for (i = 1; i <= ng_; i++)
			if ((g_top[i] SUBSEP tolower(g_stem[i])) in ARTF)
				print g_r[i], "cover", 0, ARTF[g_top[i] SUBSEP tolower(g_stem[i])], dirname(g_dest[i]) "/.media/" g_stem[i] ".png" > planf
	for (d in inner) if (!(tolower(substr(inner[d], 1, length(inner[d]) - 3)) in topsh))
		left("ports", "a port with its script only inside its folder, so not on the shelf: " d "/" inner[d], 0, "ports/" d)
}
AWK
)
table > "$DL/table.txt"
awk -v covers="$COVERS" -v nolists="$NOLISTS" -v planf="$DL/plan.tsv" -v leftf="$DL/left.tsv" \
	"$PLAN_AWK" "$DL/table.txt" "$DL/neogeo-sets.txt" "$DL/arcade-sets.txt" "$DL/mame-sets.txt" \
	"$DL/bios-sets.txt" "$DL/rogue.tsv"

# ---- the copies
size_mb() { echo $((($1 + 524288) / 1048576)); }
free_kb() { df -Pk "$CARD" | awk 'NR==2 {print $4}'; }
: > "$DL/done.tsv"
: > "$DL/newdirs.txt"
ncopied=0 bcopied=0

# done ROW KIND STATUS SIZE - what one put did, for the report
done_() { echo "$1$TAB$2$TAB$3$TAB$4" >> "$DL/done.tsv"; }

# put ROW KIND SIZE SRC DEST - one file, never over one already there, never
# past the reserve
put() {
	case $5 in "$rogue_root"/*) echo "from-rogue: refusing to write $5" >&2; exit 1 ;; esac
	if [ -e "$5" ]; then done_ "$1" "$2" present "$3"; return; fi
	if [ "$DRY" -eq 1 ]; then
		[ "$VERBOSE" -eq 1 ] && echo "  would copy ${4#"$ROGUE"/} -> ${5#"$CARD"/}"
		done_ "$1" "$2" new "$3"
		return
	fi
	if [ $(( $(free_kb) - $3 / 1024 )) -lt $((RESERVE_MB * 1024)) ]; then
		done_ "$1" "$2" noroom "$3"
		echo "  no room: ${4#"$ROGUE"/} ($(size_mb "$3") MB)"
		return
	fi
	[ "$VERBOSE" -eq 1 ] && echo "  ${4#"$ROGUE"/} -> ${5#"$CARD"/}"
	dir=$(dirname "$5")
	if [ ! -d "$dir" ]; then
		# the folders this makes, for their ._ shadows below
		x=$dir
		while [ ! -d "$x" ]; do echo "$x" >> "$DL/newdirs.txt"; x=$(dirname "$x"); done
		mkdir -p "$dir"
	fi
	# Through a .part, so a pulled card never leaves a half-copied ROM under a
	# name the shelf would list; with ROGUE's time, for the shelf's newest first
	if $CP "$4" "$5.part" && touch -r "$4" "$5.part" && mv "$5.part" "$5"; then
		# macOS tags what some apps make with com.apple.provenance, cp -X or
		# not, and on FAT and exFAT an attribute is a ._ file beside its file
		rm -f "$dir/._${5##*/}"
		done_ "$1" "$2" new "$3"
		ncopied=$((ncopied + 1)); bcopied=$((bcopied + $3))
		[ $((ncopied % 500)) -eq 0 ] && echo "  ... $ncopied files, $(size_mb "$bcopied") MB so far"
	else
		rm -f "$5.part"
		done_ "$1" "$2" failed "$3"
		echo "  FAILED: ${4#"$ROGUE"/}" >&2
	fi
	return 0
}

echo "from $ROGUE to $CARD$([ "$DRY" -eq 1 ] && echo ' (dry run: nothing is written)')"
while IFS="$TAB" read -r row kind size src dest; do
	want "$row" || continue
	put "$WANTED_AS" "$kind" "$size" "$ROGUE/$src" "$CARD/$dest"
done < "$DL/plan.tsv"

# Each Sega CD disc on the card, copied now or before, gets its region's
# backup RAM as its own .srm, unless it has one. The region is the name's:
# No-Intro and Redump names say (USA), (Europe) or (Japan); anything else is
# U. ROGUE's discs first, so a dry run lists their saves too, then any disc
# already on the card; each name once.
if want segacd; then
	seen=/
	for d in "$ROGUE/segacd/"* "$CARD/Roms/Sega CD/"*; do
		[ -f "$d" ] || continue
		base=${d##*/}
		case $base in .*|*.part) continue ;; esac
		case $seen in *"/$base/"*) continue ;; esac
		seen=$seen$base/
		case $base in *.[cC][hH][dD]|*.[cC][uU][eE]|*.[iI][sS][oO]|*.[mM]3[uU]) ;; *) continue ;; esac
		case $base in *"(Japan"*) r=J ;; *"(Europe"*) r=E ;; *) r=U ;; esac
		brm=$ROGUE/bios/scd_$r.brm
		[ -f "$brm" ] && put segacd save "$(ls -ln "$brm" | awk '{print $5}')" "$brm" "$CARD/Saves/${base%.*}.srm"
	done
fi

# And the ._ shadows of the folders this run made
while IFS= read -r d; do rm -f "$(dirname "$d")/._${d##*/}"; done < "$DL/newdirs.txt"

# ---- favorites: listed as ZyperOS keys them (src/favorites.h), fav.<tag>, a
# tab, and the file under the shelf's folder, the tag systems.cfg's
CFG=$(dirname "$0")/../config/systems.cfg
: > "$DL/favorites.tsv"
if [ -f "$CFG" ]; then
	for gl in "$ROGUE"/*/gamelist.xml; do
		[ -f "$gl" ] || continue
		f=${gl%/gamelist.xml}; f=${f##*/}
		# A game's fields, gathered until its </game>: ES writes one per
		# line, but nothing says it must
		awk -v top="$f" '
			{ buf = buf " " $0 }
			{
				while ((j = index(buf, "</game>")) > 0) {
					g = substr(buf, 1, j - 1); buf = substr(buf, j + 7)
					p = index(g, "<path>"); q = index(g, "</path>")
					if (g !~ /<favorite>[ \t]*true[ \t]*<\/favorite>/ || !p || q <= p) continue
					path = substr(g, p + 6, q - p - 6)
					gsub(/&amp;/, "\\&", path); gsub(/&apos;/, "'\''", path); gsub(/&quot;/, "\"", path)
					gsub(/&lt;/, "<", path); gsub(/&gt;/, ">", path)
					sub(/^[ \t]*\.\//, "", path); sub(/^[ \t]*\/storage\/roms\/[^\/]*\//, "", path); sub(/[ \t]*$/, "", path)
					print top "/" path
				}
				if (index(buf, "<game") == 0) buf = ""
			}' "$gl"
	done > "$DL/favorite-paths.txt"
	awk -v OFS='\t' -v cfg="$CFG" -v favp="$DL/favorite-paths.txt" '
		FILENAME == cfg { if ($1 == "sys") tag[$3] = $5; next }
		FILENAME == favp { want[$0] = 1; next }
		$2 == "game" && ($4 in want) {
			folder = $5; sub(/^Roms\//, "", folder); file = folder
			sub(/\/.*/, "", folder); sub(/^[^\/]*\//, "", file)
			if (folder in tag) print $1, "fav." tag[folder] "\t" file
		}' FS='|' "$CFG" FS='\t' "$DL/favorite-paths.txt" "$DL/plan.tsv" > "$DL/favorites.tsv"
fi

# ---- the report, per ROGUE folder in the table's order
awk -F'\t' -v dry="$DRY" -v want=" $WANT " -v table="$DL/table.txt" -v favs="$DL/favorites.tsv" \
    -v needf="$DL/need.txt" '
function mb(b) { return sprintf("%d MB", (b + 524288) / 1048576) }
function n_mb(n, b) { return n " (" mb(b) ")" }
BEGIN {
	while ((getline line < table) > 0) {
		if (line ~ /^#/ || line == "") continue
		split(line, f, "|")
		if (f[1] == "bios") continue
		nr++; row[nr] = f[2]; kind[f[2]] = f[1]; place[f[2]] = f[3]
	}
	while ((getline line < favs) > 0) { split(line, f, "\t"); nfav[f[1]]++ }
}
FNR == 1 { file_no++ }
file_no == 1 {                                   # plan.tsv
	if (kind[$1] == "pending" && $2 == "game") { pn[$1]++; pb[$1] += $3 }
	seen[$1] = 1; next
}
file_no == 2 {                                   # done.tsv
	k = $1 SUBSEP $2 SUBSEP $3; cnt[k]++; byt[k] += $4
	if ($3 == "new") { tot += $4; nnew++ } else if ($3 == "noroom") noroom++; else if ($3 == "failed") failed++
	seen[$1] = 1; next
}
{                                                # left.tsv
	k = $1 SUBSEP $2
	if (!(k in lcnt)) order[$1] = order[$1] SUBSEP $2
	lcnt[k]++; lbyt[k] += $3; seen[$1] = 1
}
function line_for(r, what, label,    s, st, k, a) {
	s = ""
	split("new present noroom failed", st, " ")
	for (a = 1; a <= 4; a++) {
		k = r SUBSEP what SUBSEP st[a]
		if (cnt[k]) s = s (s == "" ? "" : ", ") n_mb(cnt[k], byt[k]) " " \
			(st[a] == "new" ? (dry ? "to copy" : "copied") : st[a] == "present" ? "already there" : st[a] == "noroom" ? "NO ROOM" : "FAILED")
	}
	if (s != "") print "    " label ": " s
}
function lefts(r,    m, w, i, k) {
	m = split(order[r], w, SUBSEP)
	for (i = 2; i <= m; i++) { k = r SUBSEP w[i]; print "    left: " w[i] ": " n_mb(lcnt[k], lbyt[k]) }
}
END {
	for (i = 1; i <= nr; i++) {
		r = row[i]
		if (!(r in seen)) continue
		if (kind[r] == "pending") {
			print r " -> " place[r] ": TODO, ZyperOS has no shelf for it yet"
			if (pn[r] && !index(want, " " r " ")) print "    games: " n_mb(pn[r], pb[r]) " on ROGUE, not copied (name it to copy them to " place[r] ")"
		} else if (kind[r] == "skip") print r ": not copied, by decision"
		else if (kind[r] == "other") print r ":"
		else if (index(want, " " r " ")) print r " -> " place[r] (kind[r] == "sets" ? " or Neo Geo or MAME, by set list" : "") ":"
		else continue
		line_for(r, "game", kind[r] == "port" ? "ports" : kind[r] ~ /^(bgm|music)$/ ? "tracks" : "games")
		line_for(r, "track", kind[r] == "port" ? "port files" : "track files")
		line_for(r, "save", "saves")
		line_for(r, "bios", "BIOS")
		line_for(r, "cover", "covers")
		if (nfav[r]) print "    favorites in ROGUE: " nfav[r] " (in favorites.tsv, not written)"
		lefts(r)
	}
	for (r in seen) if (!(r in kind)) { print r ": no row in the table"; lefts(r) }
	print ""
	print (dry ? "would copy " : "copied ") n_mb(nnew + 0, tot) (noroom ? ", " noroom " with no room" : "") (failed ? ", " failed " FAILED" : "")
	print int(tot / 1048576 + 1) > needf
}' "$DL/plan.tsv" "$DL/done.tsv" "$DL/left.tsv"

if [ "$DRY" -eq 1 ]; then
	need=$(cat "$DL/need.txt")
	free=$(( $(free_kb) / 1024 ))
	if [ "$need" -le $((free - RESERVE_MB)) ]; then
		echo "$CARD has $free MB free: it fits, with $RESERVE_MB MB kept free"
	else
		echo "$CARD has $free MB free: it does NOT fit; name fewer folders"
	fi
fi
