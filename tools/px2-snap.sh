#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# px2-snap: one game's saves, states and records set aside on the card, and
# put back byte for byte, so a test can play it and leave it as it was.
#
#   px2-snap save ROM [NAME] [--force]   (NAME: the ROM's name; --force
#                                         replaces a snapshot of that name)
#   px2-snap restore NAME [--no-restart]
#   px2-snap list
#   px2-snap delete NAME
#
# ROM is the game's file as the launcher starts it:
# /mnt/SDCARD/Roms/NES/Contra (USA).nes, Roms/NES/Contra (USA).nes or
# NES/Contra (USA).nes. A snapshot is a folder in
# /mnt/SDCARD/.userdata/px2-snap/NAME: files/ holds each of the game's files
# at its place on the card, rows its rows in the library database, list every
# file it can have and meta what the game is.
#
# A game's files, as ZyperOS keeps them (Delete Game's list, src/gamedel.c):
#
#   Saves/<stem>.srm .sav .dsv         the battery save Diatom keeps, a .sav,
#                                      DraStic's .dsv; and by the core, fbneo's
#                                      Saves/fbneo/, MAME 2003-Plus's
#                                      Saves/mame2003-plus/, Flycast's NAOMI
#                                      and Atomiswave Saves/reicast/, OpenBOR's
#                                      Saves/openbor/Saves/
#   .userdata/shared/.zyperos/<folder>/<name>.auto.state and .bmp, .1 to .6
#                                      the Auto state (resume), the six slots
#                                      (the quick slot is one of them) and
#                                      their pictures, and .auto.lang (a DS
#                                      game's language)
#   the library database               its play sessions (its play time and
#                                      when it was last played), its favorite
#                                      and its N64 Pak, read and written by the
#                                      launcher (zyperos.elf --game-rows,
#                                      src/gamerows.h): there is no sqlite3
#
# What it does not keep: what several games share (a Dreamcast's VMU in
# Bios/dc, a system's settings), and the game itself. Restore puts back every
# file the snapshot has, checked with cmp, and removes those of the game's it
# did not have, which the test made; then the rows, and the launcher starts
# again if they changed, since it holds favorites and play time from its start
# (--no-restart leaves it). Both refuse a game that is open: Diatom's now file
# (/tmp/diatom-now, diatom/src/now.h) names it, px2-open --status does, or a
# process was started with it.
#
# A busybox sh script; run as root on the device. PX2_SNAP_CARD,
# PX2_SNAP_ELF and PX2_SNAP_NOW put the card, the launcher and the now file
# elsewhere, for tools/px2-snap-check.sh.
set -u

CARD=${PX2_SNAP_CARD:-/mnt/SDCARD}
ELF=${PX2_SNAP_ELF:-$CARD/ZyperOS/zyperos.elf}
NOW=${PX2_SNAP_NOW:-/tmp/diatom-now}
CFG=$CARD/ZyperOS/systems.cfg
SNAPS=$CARD/.userdata/px2-snap
STATES=.userdata/shared/.zyperos
NL='
'

die() { echo "px2-snap: $*" >&2; exit 1; }

usage() {
	sed -n '6,10p' "$0" | sed 's/^# *//' >&2
	exit 2
}

name_ok() {
	case $1 in '' | .* | *[!A-Za-z0-9._-]*) return 1 ;; esac
	return 0
}

# The ROM's folder in Roms/ and its file there, as FOLDER and FILE
split_rom() {
	r=$1
	case $r in
	"$CARD"/Roms/*) r=${r#"$CARD"/Roms/} ;;
	Roms/*) r=${r#Roms/} ;;
	/*) die "$1 is not in $CARD/Roms" ;;
	esac
	case $r in */*) ;; *) die "$1: no system folder in Roms/ before the file" ;; esac
	case $r in *"$NL"*) die "a name with a newline in it" ;; esac
	FOLDER=${r%%/*}
	FILE=${r#*/}
	ROM=$CARD/Roms/$FOLDER/$FILE
	case /$FILE/ in */../* | */./* | *//*) die "$1: not a plain path" ;; esac
}

# The system's tag, core and extensions from systems.cfg, by its folder
system_of() {
	[ -f "$CFG" ] || die "no $CFG"
	line=$(awk -F'|' -v f="$FOLDER" '$1 == "sys" && $3 == f { print $5 "|" $4 "|" $8; exit }' "$CFG")
	[ -n "$line" ] || die "no system in systems.cfg has the folder Roms/$FOLDER"
	TAG=${line%%|*}
	line=${line#*|}
	CORE=${line%%|*}
	CORE=${CORE%%,*}
	EXTS=${line#*|}
}

# Every file of the game's, relative to the card, a line each (see the top)
candidates() {
	base=${FILE##*/}
	stem=${base%.*}
	[ -n "$stem" ] || stem=$base
	ext=${base##*.}
	sbase=$base
	case $FILE in */*)
		case ",$EXTS," in *,scummvm,*) sbase=${FILE%%/*} ;; esac ;;
	esac
	for e in srm sav dsv; do echo "Saves/$stem.$e"; done
	case $CORE in
	fbneo) for e in nv hi fs memcard; do echo "Saves/fbneo/$stem.$e"; done ;;
	mame2003_plus)
		echo "Saves/mame2003-plus/nvram/$stem.nv"
		echo "Saves/mame2003-plus/hi/$stem.hi"
		echo "Saves/mame2003-plus/cfg/$stem.cfg"
		echo "Saves/mame2003-plus/diff/$stem.dif" ;;
	flycast*)
		case $FILE in */*) ;; *)
			case $ext in zip | 7z)
				for e in eeprom nvmem nvmem2; do echo "Saves/reicast/$FILE.$e"; done ;;
			esac ;;
		esac ;;
	esac
	case $CORE:$ext in *OpenBOR*:pak)
		for e in sav hi scr cfg inp controls; do echo "Saves/openbor/Saves/$stem.$e"; done ;;
	esac
	for s in auto 1 2 3 4 5 6; do
		echo "$STATES/$FOLDER/$sbase.$s.state"
		echo "$STATES/$FOLDER/$sbase.$s.bmp"
	done
	echo "$STATES/$FOLDER/$sbase.auto.lang"
}

# Whether the game is open, said on stderr
game_open() {
	if [ -f "$NOW" ]; then
		pid=$(sed -n 's/^pid=//p' "$NOW")
		rom=$(sed -n 's/^rom=//p' "$NOW")
		if [ "$rom" = "$ROM" ] && [ -n "$pid" ] && kill -0 "$pid" 2> /dev/null; then
			echo "px2-snap: Diatom has $FILE open (pid $pid); quit it first" >&2
			return 0
		fi
	fi
	if command -v px2-open > /dev/null 2>&1; then
		st=$(px2-open --status 2> /dev/null)
		case $st in *"$ROM"* | *"$FOLDER/$FILE"*)
			echo "px2-snap: px2-open --status says $FILE is open; quit it first" >&2
			return 0 ;;
		esac
	fi
	for c in /proc/[0-9]*/cmdline; do
		[ -e "$c" ] || continue
		args=$(tr '\0' '\n' < "$c" 2> /dev/null) || continue
		case $args in *px2-snap*) continue ;; esac
		if printf '%s\n' "$args" | grep -qxF -- "$ROM"; then
			p=${c#/proc/}
			echo "px2-snap: process ${p%/cmdline} was started with $FILE; quit it first" >&2
			return 0
		fi
	done
	return 1
}

# The launcher answers --game-rows: one from before it would start a second
# launcher for an argument it does not know
rows_ok() { [ -x "$ELF" ] && grep -q -- '--game-rows-put' "$ELF"; }

cmd_save() {
	[ $# -ge 1 ] || usage
	force=
	rom_arg=
	name=
	for a in "$@"; do
		case $a in
		--force) force=1 ;;
		-*) usage ;;
		*) if [ -z "$rom_arg" ]; then rom_arg=$a; elif [ -z "$name" ]; then name=$a; else usage; fi ;;
		esac
	done
	split_rom "$rom_arg"
	[ -f "$ROM" ] || [ -d "$ROM" ] || die "no $ROM"
	system_of
	if [ -z "$name" ]; then
		b=${FILE##*/}
		name=$(printf '%s' "${b%.*}" | tr -cs 'A-Za-z0-9._-' '_' | sed 's/^[._]*//; s/_*$//')
	fi
	name_ok "$name" || die "'$name' is not a snapshot name: letters, digits, . _ and -"
	dir=$SNAPS/$name
	[ -e "$dir" ] && [ -z "$force" ] && die "$name exists; --force replaces it"
	game_open && exit 1

	tmp=$SNAPS/.$name.new
	rm -rf "$tmp"
	mkdir -p "$tmp/files" || die "cannot make $tmp"
	candidates > "$tmp/list"
	n=0
	while IFS= read -r rel; do
		[ -f "$CARD/$rel" ] || continue
		mkdir -p "$tmp/files/${rel%/*}" && cp -p "$CARD/$rel" "$tmp/files/$rel" &&
			cmp -s "$CARD/$rel" "$tmp/files/$rel" || { rm -rf "$tmp"; die "could not copy $rel"; }
		n=$((n + 1))
	done < "$tmp/list"
	rows=no
	if rows_ok; then
		"$ELF" --game-rows "$TAG" "$FILE" > "$tmp/rows" || { rm -rf "$tmp"; die "zyperos.elf --game-rows failed"; }
		rows=$(grep -c . "$tmp/rows")
	else
		echo "px2-snap: this launcher has no --game-rows; play time, favorite and Pak not kept" >&2
	fi
	{
		echo "rom=$ROM"
		echo "folder=$FOLDER"
		echo "file=$FILE"
		echo "tag=$TAG"
		echo "saved=$(date '+%Y-%m-%d %H:%M:%S')"
	} > "$tmp/meta"
	rm -rf "$dir"
	mv "$tmp" "$dir" || die "cannot move the snapshot into $dir"
	sync
	echo "px2-snap: $name: $n file(s), $rows row(s) of $TAG $FILE"
}

cmd_restore() {
	[ $# -ge 1 ] || usage
	restart=1
	name=
	for a in "$@"; do
		case $a in
		--no-restart) restart= ;;
		-*) usage ;;
		*) [ -z "$name" ] || usage; name=$a ;;
		esac
	done
	name_ok "$name" || die "'$name' is not a snapshot name"
	dir=$SNAPS/$name
	[ -f "$dir/meta" ] && [ -f "$dir/list" ] || die "no snapshot $name (px2-snap list)"
	FOLDER=$(sed -n 's/^folder=//p' "$dir/meta")
	FILE=$(sed -n 's/^file=//p' "$dir/meta")
	TAG=$(sed -n 's/^tag=//p' "$dir/meta")
	ROM=$CARD/Roms/$FOLDER/$FILE
	game_open && exit 1

	back=0 gone=0
	while IFS= read -r rel; do
		if [ -f "$dir/files/$rel" ]; then
			if cmp -s "$dir/files/$rel" "$CARD/$rel" 2> /dev/null; then continue; fi
			mkdir -p "$CARD/${rel%/*}" &&
				cp -p "$dir/files/$rel" "$CARD/$rel.px2-snap" &&
				mv -f "$CARD/$rel.px2-snap" "$CARD/$rel" &&
				cmp -s "$dir/files/$rel" "$CARD/$rel" || die "could not put back $rel"
			back=$((back + 1))
		elif [ -e "$CARD/$rel" ]; then
			rm -f "$CARD/$rel" || die "could not remove $rel"
			gone=$((gone + 1))
		fi
	done < "$dir/list"
	changed=
	if [ -f "$dir/rows" ]; then
		rows_ok || die "this launcher has no --game-rows-put to put the rows back"
		now=$("$ELF" --game-rows "$TAG" "$FILE") || die "zyperos.elf --game-rows failed"
		if [ "$now" != "$(cat "$dir/rows")" ]; then
			"$ELF" --game-rows-put "$TAG" "$FILE" < "$dir/rows" 2> /dev/null ||
				die "zyperos.elf --game-rows-put refused the rows"
			changed=1
		fi
	fi
	sync
	echo "px2-snap: $name: $back file(s) put back, $gone removed${changed:+, the rows put back}"
	if [ -n "$changed" ] && [ -n "$restart" ] && pid=$(pidof zyperos.elf); then
		# launch.sh starts it again; five starts under 5 s in a row power
		# the handheld off, so never one that has only just started
		up=$(cut -d' ' -f22 "/proc/${pid%% *}/stat" 2> /dev/null)
		hz=100
		now_t=$(cut -d. -f1 /proc/uptime)
		[ -n "$up" ] && [ $((now_t - up / hz)) -lt 6 ] && sleep 6
		kill ${pid%% *} 2> /dev/null && echo "px2-snap: the launcher is starting again"
	fi
}

cmd_list() {
	[ -d "$SNAPS" ] || { echo "px2-snap: none"; return 0; }
	found=
	for d in "$SNAPS"/*/; do
		[ -f "$d/meta" ] || continue
		found=1
		n=$(find "$d/files" -type f 2> /dev/null | wc -l)
		r=$( [ -f "$d/rows" ] && grep -c . "$d/rows" || echo -)
		printf '%-24s %s  %s file(s), %s row(s)  %s\n' "$(basename "$d")" \
			"$(sed -n 's/^saved=//p' "$d/meta")" "$((n))" "$r" "$(sed -n 's/^rom=//p' "$d/meta")"
	done
	[ -n "$found" ] || echo "px2-snap: none"
}

cmd_delete() {
	[ $# -eq 1 ] || usage
	name_ok "$1" || die "'$1' is not a snapshot name"
	[ -f "$SNAPS/$1/meta" ] || die "no snapshot $1"
	rm -rf "${SNAPS:?}/$1" && echo "px2-snap: $1 deleted"
}

[ $# -ge 1 ] || usage
cmd=$1
shift
case $cmd in
save) cmd_save "$@" ;;
restore) cmd_restore "$@" ;;
list) cmd_list ;;
delete) cmd_delete "$@" ;;
-h | --help) usage ;;
*) usage ;;
esac
