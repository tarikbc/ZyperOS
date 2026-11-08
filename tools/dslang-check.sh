#!/bin/sh
# make check-dslang, the second half
#
# The Nintendo DS's language reaches the launcher's option list
# (src/dslang.h, src/platform.c): every game of the NDS shelf is sent
# dsperate_language with the language MENU > Language saved, mapped to the
# DS's own or to English where the DS has none (Português (Brasil) and
# 简体中文), and it is sent first, so an entry of the same key in the library
# database, which comes after, wins. No other shelf gets it, and the other
# options the NDS shelf has are still sent after it, unchanged. The real
# launcher answers, through --coreopts, with the language set through
# --set (the same database MENU > Language writes).
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
mkdir -p "$D/root" "$D/Roms" "$D/userdata" "$D/shared" || exit 1
cp config/systems.cfg "$D/root/systems.cfg" || exit 1

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

if [ "$fails" -ne 0 ]; then
	printf '\n%d FAILED\n' "$fails"
	exit 1
fi
printf '\nok: the DS is told the language MENU > Language chose, or English where it has none\n'
