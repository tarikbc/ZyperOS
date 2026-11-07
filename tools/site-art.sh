#!/bin/sh
# Draw zyperos.com's icons and its social card from the art the README and
# the device already use, so the browser tab, a phone's home screen, a search
# result and a shared link all show the same Zyper:
#
#   site/favicon.svg         Zyper's head (res/web/icon.svg) on a full-bleed
#                            tile of the accent, the head inside the tile's
#                            inscribed circle: Google shows a favicon cut to
#                            a circle, so nothing may sit in the corners
#   site/favicon.ico         16, 32 and 48 px, for browsers that ask for it
#   site/favicon-96.png      the search-result size
#   site/apple-touch-icon.png  180 px; iOS rounds the corners itself
#   site/icon-192.png, site/icon-512.png  the web manifest's icons
#   site/og.png              1200 x 630, tools/site-og.html drawn
#
#   sh tools/site-art.sh
#
# Needs Google Chrome (CHROME= names another) and ImageMagick's magick. Run it
# again after the head, the lockup or the Spotlight screenshot changes, and
# commit what it writes: Cloudflare Pages' build has neither tool.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CHROME=${CHROME:-"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"}
TILE='#bef23c'
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

shot() { # shot <html> <png> <width> <height>
	"$CHROME" --headless=new --disable-gpu --hide-scrollbars \
		--allow-file-access-from-files --force-device-scale-factor=1 \
		--virtual-time-budget=8000 --user-data-dir="$TMP/profile" \
		--window-size="$3,$4" --screenshot="$2" "file://$1" >/dev/null 2>&1
	[ -s "$2" ] || { echo "site-art: Chrome drew nothing for $1" >&2; exit 1; }
}

# The head's own viewBox and drawing, set inside a 512 tile at 360 px: the
# ears reach the corners of icon.svg's square, about 0.68 of a side from its
# centre, so 360 keeps them 11 px inside the circle a 512 tile holds.
icon=$(cat "$ROOT/res/web/icon.svg")
vb=$(printf '%s' "$icon" | sed -n 's/.*viewBox="\([^"]*\)".*/\1/p')
inner=$(printf '%s' "$icon" | sed 's/^<svg[^>]*>//; s/<\/svg>[[:space:]]*$//')
cat >"$ROOT/site/favicon.svg" <<EOF
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512"><rect width="512" height="512" fill="$TILE"/><svg x="76" y="76" width="360" height="360" viewBox="$vb">$inner</svg></svg>
EOF

cat >"$TMP/icon.html" <<EOF
<!doctype html><style>html,body{margin:0;width:512px;height:512px;overflow:hidden}img{display:block}</style>
<img src="file://$ROOT/site/favicon.svg" width="512" height="512">
EOF
shot "$TMP/icon.html" "$TMP/icon-512.png" 512 512
magick "$TMP/icon-512.png" -strip "$ROOT/site/icon-512.png"
for s in 192 96; do
	name=icon-$s.png; [ $s = 96 ] && name=favicon-96.png
	magick "$TMP/icon-512.png" -filter Lanczos -resize ${s}x$s -strip "$ROOT/site/$name"
done
magick "$TMP/icon-512.png" -filter Lanczos -resize 180x180 -strip "$ROOT/site/apple-touch-icon.png"
magick "$TMP/icon-512.png" -define icon:auto-resize=48,32,16 "$ROOT/site/favicon.ico"

shot "$ROOT/tools/site-og.html" "$TMP/og.png" 1200 630
magick "$TMP/og.png" -strip "$ROOT/site/og.png"

echo "site-art: favicons and og.png in site/"
