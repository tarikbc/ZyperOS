#!/bin/sh
# make check-dslang, the second half
#
# The Nintendo DS's language reaches the launcher's option list
# (src/dslang.h, src/platform.c): every game of the NDS shelf is sent
# dsperate_language, first, from the Game Language row's setting (ds_language):
# Auto is the language MENU > Language saved, mapped to the DS's own or to
# English where the DS has none (Português (Brasil) and 简体中文); any of the
# seven languages is that language, whatever the handheld speaks; a word that
# is none is Auto. An entry of the same key in the library database comes
# after it, and wins. No other shelf gets it, and the other options the NDS
# shelf has are still sent after it, unchanged. The real launcher answers,
# through --coreopts, with the settings set through --set (the same database
# MENU writes).
#
# And the row itself, on the DS shelf's MENU in every language: it is there,
# and nothing in it is cut, slid or drawn off the screen (the launcher says
# "i18n: ..." when a string was).
#
# Needs the launcher built, which needs SDL2, and skips cleanly without it,
# as check-corner does. The card is a scratch tree under build-native/dslang-card.
set -u

D=build-native/dslang-card
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
mkdir -p "$D/root" "$D/Roms/Nintendo DS" "$D/userdata" "$D/shared" || exit 1
cp config/systems.cfg "$D/root/systems.cfg" || exit 1
: >"$D/Roms/Nintendo DS/Game.nds" || exit 1
# the shelf the menu shots stand on: the Nintendo DS's
printf 'NDS\n\n' >"$D/root/.last" || exit 1

run() {
	ZYPEROS_HEADLESS=1 SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
	ZYPEROS_ROOT="$D/root" ZYPEROS_CARD="$D" ZYPEROS_ROMS="$D/Roms" \
	ZYPEROS_USERDATA="$D/userdata" ZYPEROS_SHARED="$D/shared" ZYPEROS_WEB="$D" \
	ZYPEROS_FONT=res/fonts/menu.ttf "$BIN" "$@"
}

fails=0
want() {
	code=$1
	ds=$2
	run --set "language=$code" >/dev/null 2>&1 || { echo "  FAIL --set language=$code"; fails=$((fails + 1)); return; }
	first=$(run --coreopts NDS 2>/dev/null | sed -n 1p)
	if [ "$first" != "dsperate_language=$ds" ]; then
		echo "  FAIL $code: the NDS shelf's first option is \"$first\", not dsperate_language=$ds"
		fails=$((fails + 1))
	fi
	n=$(run --coreopts NDS 2>/dev/null | grep -c '^dsperate_language=')
	if [ "$n" != 1 ]; then
		echo "  FAIL $code: dsperate_language is sent $n times, not once"
		fails=$((fails + 1))
	fi
}

echo "every language reaches the DS as its own, or English"
want en english
want zh-Hans english
want es spanish
want pt-BR english
want fr french
want de german
want ja japanese
want it italian

echo "a language chosen outright is that language, whatever the handheld speaks"
for hand in en pt-BR zh-Hans ja; do
	run --set "language=$hand" >/dev/null 2>&1 || { echo "  FAIL --set language=$hand"; fails=$((fails + 1)); }
	for ds in english spanish french german italian japanese chinese; do
		run --set "ds_language=$ds" >/dev/null 2>&1 || { echo "  FAIL --set ds_language=$ds"; fails=$((fails + 1)); }
		first=$(run --coreopts NDS 2>/dev/null | sed -n 1p)
		if [ "$first" != "dsperate_language=$ds" ]; then
			echo "  FAIL $hand, ds_language=$ds: sent \"$first\""
			fails=$((fails + 1))
		fi
	done
done
echo "Auto, and a word that is none, follow MENU > Language"
run --set language=pt-BR >/dev/null 2>&1
for word in auto bogus Spanish firmware ""; do
	run --set "ds_language=$word" >/dev/null 2>&1
	first=$(run --coreopts NDS 2>/dev/null | sed -n 1p)
	if [ "$first" != "dsperate_language=english" ]; then
		echo "  FAIL ds_language=\"$word\" on a handheld in pt-BR: sent \"$first\", not english"
		fails=$((fails + 1))
	fi
done
run --set language=fr >/dev/null 2>&1
run --set ds_language=auto >/dev/null 2>&1
if [ "$(run --coreopts NDS 2>/dev/null | sed -n 1p)" != "dsperate_language=french" ]; then
	echo "  FAIL Auto on a handheld in fr is not french"
	fails=$((fails + 1))
fi
if ! run --dump 2>/dev/null | grep -q '^ *ds_language  *auto$'; then
	echo "  FAIL ds_language is not a setting that starts at auto"
	fails=$((fails + 1))
fi

echo "the other options are sent after it, unchanged, and no other shelf gets it"
run --set language=pt-BR >/dev/null 2>&1
all=$(run --coreopts NDS 2>/dev/null)
others=$(printf '%s\n' "$all" | sed 1d | grep -c .)
plain=$(run --coreopts NES 2>/dev/null)
nes=$(printf '%s\n' "$plain" | grep -c .)
if printf '%s\n' "$plain" | grep -q dsperate; then
	echo "  FAIL the NES shelf is sent a DS option"
	fails=$((fails + 1))
fi
if [ "$others" != "$nes" ]; then
	echo "  FAIL the NDS shelf's other options ($others) are not the NES shelf's ($nes): the global ones"
	fails=$((fails + 1))
fi
if [ "$(printf '%s\n' "$all" | sed 1d)" != "$plain" ]; then
	echo "  FAIL the options after the DS language are not the ones every shelf gets"
	fails=$((fails + 1))
fi
if run --coreopts "Nintendo DS" 2>/dev/null | grep -q dsperate; then
	echo "  FAIL a shelf is told by name, not by its tag"
	fails=$((fails + 1))
fi

echo "Game Language on the DS shelf's MENU, in every language, and Left, Right and A round it"
run --set ds_language=auto >/dev/null 2>&1
for lang in en zh-Hans es pt-BR fr de ja it; do
	run --set "language=$lang" >/dev/null 2>&1
	run --shot "$D/menu-$lang.png" --theme light --screen games --menu "Game Language" >"$D/one.log" 2>&1
	st=$?
	if [ "$st" -ne 0 ] || [ ! -s "$D/menu-$lang.png" ]; then
		echo "  FAIL $lang: the menu shot failed, exit $st"
		tail -n 3 "$D/one.log" | sed 's/^/    /'
		fails=$((fails + 1))
	elif grep -q '^i18n: ' "$D/one.log"; then
		echo "  FAIL $lang: a string did not fit:"
		grep '^i18n: ' "$D/one.log" | sed 's/^/    /' | head -3
		fails=$((fails + 1))
	fi
done
# The row is on the shelf's menu by its name, and a press goes round the
# eight and back, read from the database after each (each press its own
# process, through the row's real handler)
run --set language=en >/dev/null 2>&1
run --set ds_language=auto >/dev/null 2>&1
press() {
	key=$1
	want=$2
	run --shot "$D/press.png" --theme light --screen games --menu "Game Language" --press "$key" >"$D/one.log" 2>&1
	got=$(run --dump 2>/dev/null | awk '$1 == "ds_language" { print $2 }')
	if [ "$got" != "$want" ]; then
		echo "  FAIL $key saved \"${got:-nothing}\", not $want"
		fails=$((fails + 1))
	fi
}
press right english
press right spanish
press right french
press right german
press right italian
press right japanese
press right chinese
press right auto
press left chinese
press left japanese
press left italian
press left german
press left french
press left spanish
press left english
press left auto

if [ "$fails" -ne 0 ]; then
	printf '\n%d FAILED\n' "$fails"
	exit 1
fi
printf '\nok: the DS is told the language chosen, Auto being MENU > Language'"'"'s or English where the DS has none\n'
