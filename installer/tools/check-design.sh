#!/bin/sh
# Whether the installer's page still wears ZyperOS's design, as the rest of
# this repository has it: ui/style.css's color block against the one
# tools/designdef.py writes into res/web/app.css (make design), and the
# fonts and Zyper marks in ui/ against res/. They are copies because the
# page is built from ui/ alone (Tauri's frontendDist), on every system.
set -eu
cd "$(dirname "$0")/.."
RES=../res
fail=0
block() { sed -n '/\/\* generated:tokens \*\//,/\/\* \/generated:tokens \*\//p' "$1"; }
A=$(mktemp)
B=$(mktemp)
trap 'rm -f "$A" "$B"' EXIT
block "$RES/web/app.css" > "$A"
block ui/style.css > "$B"
if [ ! -s "$A" ]; then
	echo "check-design: $RES/web/app.css has no generated token block" >&2
	fail=1
elif ! cmp -s "$A" "$B"; then
	echo "check-design: ui/style.css's colors differ from $RES/web/app.css:" >&2
	diff "$A" "$B" >&2 || true
	echo "check-design: copy the block between /* generated:tokens */ and /* /generated:tokens */ over" >&2
	fail=1
fi
for pair in "fonts/menu.ttf ui/fonts/menu.ttf" "fonts/title.ttf ui/fonts/title.ttf" "fonts/OFL.txt ui/fonts/OFL.txt" \
	"web/mark.svg ui/mark.svg" "web/mark-light.svg ui/mark-light.svg" "web/zyper.svg ui/zyper.svg"; do
	set -- $pair
	if ! cmp -s "$RES/$1" "$2"; then
		echo "check-design: $2 differs from res/$1: copy it over" >&2
		fail=1
	fi
done
[ $fail = 0 ] && echo "check-design: the installer has ZyperOS's colors, fonts and marks"
exit $fail
