#!/bin/sh
# From Remotion's render of the Brick introduction (BrickIntro, src/brick-intro)
# to the files people see, outside the repository:
#
#   MASTER: H.264 at a high quality, the audio at -16 LUFS integrated, 2.5 dB
#     true peak headroom, as scripts/finish.sh makes the tour's
#   REDDIT: the 4:5 cut at a web quality (CRF 14, about 15 to 25 MB at 2:00),
#     its audio measured and normalized the same way
#
# Both limited range BT.709, as finish.sh says why, and each with its frame 0
# checked (not blank: Reddit and players show it before play) and saved
# beside it as NAME-poster.jpg. It refuses to run while a scene still waits
# for its feature on the Brick (scripts/check-waits.mjs).
#
#   npx remotion render BrickIntro out/brick-intro/zyperos-brick-raw.mp4 --crf=6 --concurrency=4
#   sh video/scripts/finish-brick-intro.sh [DIR]
#   DIR defaults to ~/Documents/ZyperOS/video
set -eu
VIDEO=$(cd "$(dirname "$0")/.." && pwd)
cd "$VIDEO"
node scripts/check-waits.mjs || { echo "a scene still waits: no file made" >&2; exit 1; }
RAW="$VIDEO/out/brick-intro/zyperos-brick-raw.mp4"
DIR=${1:-$HOME/Documents/ZyperOS/video}
MASTER="$DIR/zyperos-brick-master.mp4"
REDDIT="$DIR/zyperos-brick-reddit.mp4"
[ -f "$RAW" ] || { echo "no $RAW: npx remotion render BrickIntro out/brick-intro/zyperos-brick-raw.mp4" >&2; exit 1; }
mkdir -p "$DIR"

# The loudness of the render, measured (loudnorm's first pass), as the filter that applies it exactly
stats=$(ffmpeg -hide_banner -nostats -i "$RAW" -af loudnorm=I=-16:TP=-2.5:LRA=11:print_format=json -f null - 2>&1 |
	sed -n '/^{/,/^}/p')
val() { printf '%s' "$stats" | sed -n "s/.*\"$1\" : \"\([^\"]*\)\".*/\1/p"; }
norm="loudnorm=I=-16:TP=-2.5:LRA=11:measured_I=$(val input_i):measured_TP=$(val input_tp):measured_LRA=$(val input_lra):measured_thresh=$(val input_thresh):offset=$(val target_offset):linear=true"
range="scale=in_range=full:out_range=limited:out_color_matrix=bt709,format=yuv420p"
tags="-color_range tv -colorspace bt709 -color_primaries bt709 -color_trc bt709 -movflags +faststart"

ffmpeg -loglevel error -y -i "$RAW" -vf "$range" -c:v libx264 -preset slow -crf "${MASTER_CRF:-6}" -profile:v high \
	-af "$norm" -ar 48000 -c:a aac -b:a 320k $tags "$MASTER"
ffmpeg -loglevel error -y -i "$RAW" -vf "$range" -c:v libx264 -preset slow -crf "${REDDIT_CRF:-14}" -profile:v high \
	-af "$norm" -ar 48000 -c:a aac -b:a 256k $tags "$REDDIT"

for f in "$MASTER" "$REDDIT"; do
	printf '%s  %s bytes  %s s\n' "$f" "$(stat -f %z "$f")" "$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$f")"
done
ffmpeg -hide_banner -nostats -i "$REDDIT" -af ebur128=peak=true -f null - 2>&1 | sed -n '/Summary/,$p' | grep -E "I:|Peak:"
python3 "$VIDEO/scripts/check-first-frame.py" "$MASTER" "$REDDIT"
