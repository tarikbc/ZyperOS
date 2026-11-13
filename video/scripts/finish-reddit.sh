#!/bin/sh
# From Remotion's render of the Reddit cut (src/reddit) to its master, which
# stays outside the repository: H.264 at 4:5 and a high quality, the audio at
# -16 LUFS integrated with 2.5 dB of true peak headroom, limited range BT.709
# (Remotion writes full range, which phones show washed out or crushed).
#
#   npx remotion render Reddit105 out/reddit/zyperos-1.0.5-reddit-raw.mp4 --crf=6
#   sh scripts/finish-reddit.sh [MASTER]
#   MASTER defaults to ~/Documents/ZyperOS/video/zyperos-1.0.5-reddit.mp4
set -eu
VIDEO=$(cd "$(dirname "$0")/.." && pwd)
RAW="$VIDEO/out/reddit/zyperos-1.0.5-reddit-raw.mp4"
MASTER=${1:-$HOME/Documents/ZyperOS/video/zyperos-1.0.5-reddit.mp4}
[ -f "$RAW" ] || { echo "no $RAW: npx remotion render Reddit105 out/reddit/zyperos-1.0.5-reddit-raw.mp4" >&2; exit 1; }
mkdir -p "$(dirname "$MASTER")"

# The loudness, measured first (loudnorm's first pass), then applied exactly
stats=$(ffmpeg -hide_banner -nostats -i "$RAW" -af loudnorm=I=-16:TP=-2.5:LRA=11:print_format=json -f null - 2>&1 |
	sed -n '/^{/,/^}/p')
val() { printf '%s' "$stats" | sed -n "s/.*\"$1\" : \"\([^\"]*\)\".*/\1/p"; }
norm="loudnorm=I=-16:TP=-2.5:LRA=11:measured_I=$(val input_i):measured_TP=$(val input_tp):measured_LRA=$(val input_lra):measured_thresh=$(val input_thresh):offset=$(val target_offset):linear=true"

ffmpeg -loglevel error -y -i "$RAW" -vf "scale=in_range=full:out_range=limited:out_color_matrix=bt709,format=yuv420p" \
	-c:v libx264 -preset slow -crf "${MASTER_CRF:-12}" -profile:v high -af "$norm" -ar 48000 -c:a aac -b:a 256k \
	-color_range tv -colorspace bt709 -color_primaries bt709 -color_trc bt709 -movflags +faststart "$MASTER"

printf '%s  %s MB  %s s\n' "$MASTER" "$(( $(stat -f %z "$MASTER") / 1048576 ))" \
	"$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$MASTER")"
ffmpeg -hide_banner -nostats -i "$MASTER" -af ebur128=peak=true -f null - 2>&1 | sed -n '/Summary/,$p' | grep -E "I:|Peak:"
