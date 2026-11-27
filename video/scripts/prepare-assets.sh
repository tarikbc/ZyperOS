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
# The 1.0.6 video's own track (src/v106/music.ts): Juhani Junkala's "Level 1",
# CC0 (https://opengameart.org/content/5-chiptunes-action), fetched once into
# a cache outside the repository and checked. The video plays it from its
# first downbeat (0.0259 s), its 56-bar loop at 181 BPM once, then from the top
# again, joined with a 12 ms crossfade centred on the bar line, so the beat
# grid holds across the join and the file's closing fade is never heard.
MUSIC_CACHE="${ZYPEROS_MUSIC_CACHE:-$HOME/Library/Caches/zyperos-video-music}"
LEVEL1="$MUSIC_CACHE/level1.mp3"
LEVEL1_SHA=559ad4eb2f936b7a3db0513aabf45926c842253a5721d23b3202a7a56a6b19da
if [ ! -f "$LEVEL1" ]; then
	mkdir -p "$MUSIC_CACHE"
	curl -sSfL --max-time 120 -o "$LEVEL1.part" https://opengameart.org/sites/default/files/level1.mp3
	mv "$LEVEL1.part" "$LEVEL1"
fi
[ "$(shasum -a 256 "$LEVEL1" | cut -d' ' -f1)" = "$LEVEL1_SHA" ] || { echo "$LEVEL1 is not the Level 1 that was measured" >&2; exit 1; }
if [ ! -f "$PUB/music/level1-loop.wav" ]; then
	set -- $(python3 -c "b=60/181*4; t0=0.0259; L=56*b; print(f'{t0:.4f} {t0+L+0.006:.4f} {t0-0.006:.4f} {t0+24*b:.4f}')")
	ffmpeg -loglevel error -y -i "$LEVEL1" -filter_complex \
		"[0:a]asplit[x][y];[x]atrim=start=$1:end=$2,asetpts=PTS-STARTPTS[a];[y]atrim=start=$3:end=$4,asetpts=PTS-STARTPTS[b];[a][b]acrossfade=d=0.012:c1=tri:c2=tri" \
		-ar 48000 "$PUB/music/level1-loop.wav"
fi
# The 1.1 videos' tracks (src/v110/music.ts, src/reddit110/music.ts), both
# Zane Little Music's, CC0 on OpenGameArt: fetched once into the same cache,
# checked, and written from each one's first downbeat, so its first sample is
# bar 0. Neither video runs past its file's end, so neither is looped.
fetch_track() { # URL FILE SHA256 FIRST_BEAT_S OUT
	if [ ! -f "$MUSIC_CACHE/$2" ]; then
		mkdir -p "$MUSIC_CACHE"
		curl -sSfL --max-time 120 -o "$MUSIC_CACHE/$2.part" "$1"
		mv "$MUSIC_CACHE/$2.part" "$MUSIC_CACHE/$2"
	fi
	[ "$(shasum -a 256 "$MUSIC_CACHE/$2" | cut -d' ' -f1)" = "$3" ] || { echo "$MUSIC_CACHE/$2 is not the track that was measured" >&2; exit 1; }
	[ -f "$PUB/music/$5" ] || ffmpeg -loglevel error -y -i "$MUSIC_CACHE/$2" -af "atrim=start=$4,asetpts=PTS-STARTPTS" -ar 48000 "$PUB/music/$5"
}
fetch_track https://opengameart.org/sites/default/files/it_takes_a_hero.mp3 it_takes_a_hero.mp3 \
	d733e5a437e875b2cf14edb954e60de6dca5a6d2621b25dfbf260098c068a209 0.0241 it-takes-a-hero.wav
fetch_track https://opengameart.org/sites/default/files/space_cadet_training_montage.mp3 space_cadet_training_montage.mp3 \
	894d706b047dd662640d9944bc60ee797898626e61e9a377ea335407dd7ee548 0.0263 space-cadet-training-montage.wav
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
# The TrimUI Brick, for the 1.0.6 video (src/v106): tools/build-brick-model.py's
# model (the site's) the same way, true to size beside the Pixel 2
if [ ! -f "$PUB/device/brick-front.png" ]; then
	command -v blender >/dev/null || { echo "Blender is needed once, for the device" >&2; exit 1; }
	work=$(mktemp -d)
	trap 'rm -rf "$work"' EXIT
	# The model's script writes beside itself, so it runs from a copy
	mkdir -p "$work/tools"
	cp "$ROOT/tools/build-brick-model.py" "$work/tools/"
	(cd "$work" && blender --background --python tools/build-brick-model.py >/dev/null)
	blender --background "$work/build/brick-model/brick.blend" \
	        --python "$VIDEO/device/render-device.py" -- "$PUB/device" 2000 >/dev/null
fi
echo "video/public is ready"
