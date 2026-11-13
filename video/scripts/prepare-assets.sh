#!/bin/sh
# Fills video/public with what the tour is made of, all of it from the
# repository or made from it, none of it committed (video/.gitignore):
#
#   fonts/   Lexend and the CJK menu fonts (res/fonts)
#   img/     the ZyperOS lockups, the File Transfer and Player 2 pages' shots and the shaders' (res/readme),
#            and Sonic 2's and Super Mario Bros. 3's libretro screenshots from the art cache video/footage/card.py fills
#   boot/    the boot animation (res/boot)
#   music/   ZyperOS's menu music tracks (res/bgm): src/music.ts picks one
#   audio/   the launcher's own interface sounds (res/sounds/wood)
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
# The File Transfer page in a browser, and NES Player 2's play page on a phone, as tools/readme-shots.py drew them
cp "$ROOT/res/readme/portal.png" "$ROOT/res/readme/portal-phone.png" "$ROOT/res/readme/portal-pin.png" \
   "$ROOT/res/readme/play-phone.png" "$PUB/img/"
cp "$ROOT/res/boot/zyperos-fly.mp4" "$PUB/boot/"
mkdir -p "$PUB/music"
cp "$ROOT"/res/bgm/*.mp3 "$PUB/music/"
cp "$ROOT"/res/sounds/wood/*.wav "$PUB/audio/ui/"
snap="$ART/Sega_-_Mega_Drive_-_Genesis/Named_Snaps/Sonic The Hedgehog 2 (World).png"
[ -f "$snap" ] || { echo "no $snap: run video/footage/takes.py first, which fills the art cache" >&2; exit 1; }
cp "$snap" "$PUB/img/sonic2.png"
# Super Mario Bros. 3's, the NES game Player 2 joins (footage/takes.py's player2)
snap="$ART/Nintendo_-_Nintendo_Entertainment_System/Named_Snaps/Super Mario Bros. 3 (USA).png"
[ -f "$snap" ] || { echo "no $snap: run video/footage/takes.py first, which fills the art cache" >&2; exit 1; }
cp "$snap" "$PUB/img/smb3.png"
# The Shader row's six looks on Balloon Fight, the README's picture of them
# (made in the QEMU machine with Diatom's own shaders) cut into one each, at
# the screen's size, for the Reddit cut's shaders scene (src/reddit)
i=0
for name in off sharp crt crt-curved scanlines lcd; do
	x=$((10 + (i % 3) * 448)); y=$((10 + (i / 3) * 372))
	ffmpeg -loglevel error -y -i "$ROOT/res/readme/shaders.png" \
		-vf "crop=428:320:$x:$y,scale=1076:807:flags=lanczos" "$PUB/img/shader-$name.png"
	i=$((i + 1))
done

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
