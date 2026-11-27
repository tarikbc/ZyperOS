#!/bin/sh
# From Remotion's renders of the 1.1 videos (Release110, the Discord's, and
# Reddit110, the Reddit cut) to the files people see, all outside the
# repository:
#
#   MASTER: H.264 at a high quality, the audio at -16 LUFS integrated, 2.5 dB
#     true peak headroom, as scripts/finish.sh makes the tour's
#   DISCORD: the same at 10 MB or less, the most a Discord account without
#     Nitro can upload: two-pass x264 at the bitrate that size leaves, 1080p
#     (DISCORD_HEIGHT=720 for 720p), AAC at 96 kb/s
#   REDDIT: the 4:5 cut at a web quality, about 15 MB (scripts/finish-reddit.sh's
#     settings), its audio measured and normalized the same way
#
# Both limited range BT.709, as finish.sh says why, and each with its frame 0
# checked (not blank: Discord and players show it before play) and saved
# beside it as NAME-poster.jpg.
#
#   npx remotion render Release110 out/v110/zyperos-1.1-raw.mp4 --crf=6 --concurrency=4
#   npx remotion render Reddit110 out/reddit110/zyperos-1.1-reddit-raw.mp4 --crf=6 --concurrency=4
#   sh video/scripts/finish-110.sh [DIR]
#   DIR defaults to ~/Documents/ZyperOS/video
set -eu
VIDEO=$(cd "$(dirname "$0")/.." && pwd)
RAW="$VIDEO/out/v110/zyperos-1.1-raw.mp4"
RAW_REDDIT="$VIDEO/out/reddit110/zyperos-1.1-reddit-raw.mp4"
DIR=${1:-$HOME/Documents/ZyperOS/video}
MASTER="$DIR/zyperos-1.1-master.mp4"
DISCORD="$DIR/zyperos-1.1-discord.mp4"
REDDIT="$DIR/zyperos-1.1-reddit.mp4"
# Bytes the Discord file may take: under 10 MB, with room for the container
LIMIT=${DISCORD_BYTES:-9500000}
HEIGHT=${DISCORD_HEIGHT:-1080}
[ -f "$RAW" ] || { echo "no $RAW: npx remotion render Release110 out/v110/zyperos-1.1-raw.mp4" >&2; exit 1; }
[ -f "$RAW_REDDIT" ] || { echo "no $RAW_REDDIT: npx remotion render Reddit110 out/reddit110/zyperos-1.1-reddit-raw.mp4" >&2; exit 1; }
mkdir -p "$DIR"

# The loudness of a render, measured (loudnorm's first pass), as the filter that applies it exactly
norm_of() {
	stats=$(ffmpeg -hide_banner -nostats -i "$1" -af loudnorm=I=-16:TP=-2.5:LRA=11:print_format=json -f null - 2>&1 |
		sed -n '/^{/,/^}/p')
	val() { printf '%s' "$stats" | sed -n "s/.*\"$1\" : \"\([^\"]*\)\".*/\1/p"; }
	echo "loudnorm=I=-16:TP=-2.5:LRA=11:measured_I=$(val input_i):measured_TP=$(val input_tp):measured_LRA=$(val input_lra):measured_thresh=$(val input_thresh):offset=$(val target_offset):linear=true"
}
norm=$(norm_of "$RAW")
range="scale=in_range=full:out_range=limited:out_color_matrix=bt709,format=yuv420p"
tags="-color_range tv -colorspace bt709 -color_primaries bt709 -color_trc bt709 -movflags +faststart"

ffmpeg -loglevel error -y -i "$RAW" -vf "$range" -c:v libx264 -preset slow -crf "${MASTER_CRF:-6}" -profile:v high \
	-af "$norm" -ar 48000 -c:a aac -b:a 320k $tags "$MASTER"

# The video's share of the size: what the audio leaves, less 2% for the container
dur=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$MASTER")
kbps=$(python3 -c "print(int(($LIMIT * 8 * 0.98 / $dur - 96000) / 1000))")
pass=$(mktemp -d)
trap 'rm -rf "$pass"' EXIT
scale="scale=-2:$HEIGHT:flags=lanczos"
# The same encoder settings in both passes: the second refuses another keyint
x264="-c:v libx264 -preset veryslow -tune animation -b:v ${kbps}k -maxrate $((kbps * 3))k -bufsize $((kbps * 4))k
	-profile:v high -pix_fmt yuv420p -x264-params keyint=60"
ffmpeg -loglevel error -y -i "$MASTER" -vf "$scale" $x264 -pass 1 -passlogfile "$pass/x264" -an -f mp4 /dev/null
ffmpeg -loglevel error -y -i "$MASTER" -vf "$scale" $x264 -pass 2 -passlogfile "$pass/x264" -c:a aac -b:a 96k $tags "$DISCORD"

# The Reddit cut: 4:5 at a web quality (about 15 MB), as finish-reddit.sh makes 1.0.5's
ffmpeg -loglevel error -y -i "$RAW_REDDIT" -vf "$range" -c:v libx264 -preset slow -crf "${REDDIT_CRF:-12}" -profile:v high \
	-af "$(norm_of "$RAW_REDDIT")" -ar 48000 -c:a aac -b:a 256k $tags "$REDDIT"

for f in "$MASTER" "$DISCORD" "$REDDIT"; do
	printf '%s  %s bytes  %s s\n' "$f" "$(stat -f %z "$f")" "$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$f")"
done
size=$(stat -f %z "$DISCORD")
[ "$size" -le 10000000 ] || { echo "the Discord file is $size bytes, over 10 MB" >&2; exit 1; }
ffmpeg -hide_banner -nostats -i "$DISCORD" -af ebur128=peak=true -f null - 2>&1 | sed -n '/Summary/,$p' | grep -E "I:|Peak:"
# Frame 0 is the picture Discord shows before play: a designed frame, not a
# blank one, each saved beside its video as its poster
python3 "$VIDEO/scripts/check-first-frame.py" "$MASTER" "$DISCORD" "$REDDIT"
