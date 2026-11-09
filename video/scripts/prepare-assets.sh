#!/bin/sh
# Fills video/public with what the tour is made of, all of it from the
# repository or made from it, none of it committed (video/.gitignore):
#
#   fonts/   Lexend and the CJK menu fonts (res/fonts)
#   img/     the ZyperOS lockups and the File Transfer page's shots (res/readme), and Sonic 2's libretro screenshot
#            from the art cache video/footage/card.py fills
#   boot/    the boot animation (res/boot)
#   audio/   the menu music track the video plays (res/bgm) and the launcher's
#            own interface sounds (res/sounds/wood)
#   device/  the Pixel 2 straight on, its screen cut out, rendered in Blender
#            from tools/build-pixel2-model.py's scene (video/device), once
#
# The footage is video/footage/takes.py's. Run from anywhere:
#   sh video/scripts/prepare-assets.sh
set -eu
VIDEO=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$VIDEO/.." && pwd)
PUB="$VIDEO/public"
ART="${ZYPEROS_ART_CACHE:-$HOME/Library/Caches/zyperos-readme-art}"

mkdir -p "$PUB/fonts" "$PUB/img" "$PUB/boot" "$PUB/audio/ui" "$PUB/device"
cp "$ROOT/res/fonts/menu.ttf" "$ROOT/res/fonts/title.ttf" "$ROOT/res/fonts/ja-menu.otf" \
   "$ROOT/res/fonts/zh-menu.otf" "$PUB/fonts/"
cp "$ROOT/res/readme/lockup.png" "$ROOT/res/readme/lockup-light.png" "$PUB/img/"
# The File Transfer page in a browser, as tools/readme-shots.py drew it
cp "$ROOT/res/readme/portal.png" "$ROOT/res/readme/portal-phone.png" "$ROOT/res/readme/portal-pin.png" "$PUB/img/"
cp "$ROOT/res/boot/zyperos-fly.mp4" "$PUB/boot/"
cp "$ROOT/res/bgm/The Arplands.mp3" "$PUB/audio/music.mp3"
cp "$ROOT"/res/sounds/wood/*.wav "$PUB/audio/ui/"
snap="$ART/Sega_-_Mega_Drive_-_Genesis/Named_Snaps/Sonic The Hedgehog 2 (World).png"
[ -f "$snap" ] || { echo "no $snap: run video/footage/takes.py first, which fills the art cache" >&2; exit 1; }
cp "$snap" "$PUB/img/sonic2.png"

if [ ! -f "$PUB/device/pixel2-front.png" ]; then
	command -v blender >/dev/null || { echo "Blender is needed once, for the device" >&2; exit 1; }
	work=$(mktemp -d)
	trap 'rm -rf "$work"' EXIT
	# The model's script writes beside itself, so it runs from a copy
	mkdir -p "$work/tools" "$work/res/readme"
	cp "$ROOT/tools/build-pixel2-model.py" "$work/tools/"
	cp "$ROOT/res/readme/systems.png" "$work/res/readme/"
	(cd "$work" && blender --background --python tools/build-pixel2-model.py >/dev/null)
	blender --background "$work/build/pixel2-model/pixel2.blend" \
	        --python "$VIDEO/device/render-device.py" -- "$PUB/device" 2000 >/dev/null
fi
echo "video/public is ready"
