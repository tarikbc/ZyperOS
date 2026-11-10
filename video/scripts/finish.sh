#!/bin/sh
# From Remotion's render to the files people see:
#
#   MASTER (outside the repository, for YouTube and creators): H.264 at a high
#     quality, the audio at -16 LUFS integrated, 2.5 dB true peak headroom (AAC at web size overshoots)
#   site/zyperos-tour.mp4: the same at a web size for zyperos.com (under
#     25 MB, Cloudflare Pages' file limit), with its poster site/zyperos-tour.jpg
#   res/readme/tour-poster.jpg: the README's poster, with a play button, which
#     links to the video on zyperos.com
#
# Both MP4s are limited range BT.709: Remotion writes full range (yuvj420p),
# which browsers and players show washed out or crushed when they assume the
# usual range.
#
#   sh video/scripts/finish.sh [MASTER]
#   MASTER defaults to ~/Documents/ZyperOS/video/zyperos-tour-master.mp4
set -eu
VIDEO=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$VIDEO/.." && pwd)
RAW="$VIDEO/out/zyperos-tour-raw.mp4"
MASTER=${1:-$HOME/Documents/ZyperOS/video/zyperos-tour-master.mp4}
WEB="$ROOT/site/zyperos-tour.mp4"
[ -f "$RAW" ] || { echo "no $RAW: npx remotion render ZyperOSTour out/zyperos-tour-raw.mp4" >&2; exit 1; }
mkdir -p "$(dirname "$MASTER")"

# The loudness, measured first (loudnorm's first pass), then applied exactly
stats=$(ffmpeg -hide_banner -nostats -i "$RAW" -af loudnorm=I=-16:TP=-2.5:LRA=11:print_format=json -f null - 2>&1 |
	sed -n '/^{/,/^}/p')
val() { printf '%s' "$stats" | sed -n "s/.*\"$1\" : \"\([^\"]*\)\".*/\1/p"; }
norm="loudnorm=I=-16:TP=-2.5:LRA=11:measured_I=$(val input_i):measured_TP=$(val input_tp):measured_LRA=$(val input_lra):measured_thresh=$(val input_thresh):offset=$(val target_offset):linear=true"
range="scale=in_range=full:out_range=limited:out_color_matrix=bt709,format=yuv420p"
tags="-color_range tv -colorspace bt709 -color_primaries bt709 -color_trc bt709 -movflags +faststart"

ffmpeg -loglevel error -y -i "$RAW" -vf "$range" -c:v libx264 -preset slow -crf "${MASTER_CRF:-6}" -profile:v high \
	-af "$norm" -ar 48000 -c:a aac -b:a 320k $tags "$MASTER"
ffmpeg -loglevel error -y -i "$MASTER" -c:v libx264 -preset veryslow -crf "${WEB_CRF:-21}" -tune animation \
	-profile:v high -pix_fmt yuv420p -x264-params keyint=60 -c:a aac -b:a 128k $tags "$WEB"

(cd "$VIDEO" && npx remotion still Poster out/poster.png >/dev/null && npx remotion still PosterPlay out/poster-play.png >/dev/null)
ffmpeg -loglevel error -y -i "$VIDEO/out/poster.png" -vf scale=1600:-2 -q:v 3 "$ROOT/site/zyperos-tour.jpg"
ffmpeg -loglevel error -y -i "$VIDEO/out/poster-play.png" -vf scale=1280:-2 -q:v 3 "$ROOT/res/readme/tour-poster.jpg"

for f in "$MASTER" "$WEB"; do
	printf '%s  %s MB  %s s\n' "$f" "$(( $(stat -f %z "$f") / 1048576 ))" \
		"$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$f")"
done
ffmpeg -hide_banner -nostats -i "$WEB" -af ebur128=peak=true -f null - 2>&1 | sed -n '/Summary/,$p' | grep -E "I:|Peak:"
