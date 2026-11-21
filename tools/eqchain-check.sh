#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# A port's ALSA chain (sd/zyperos/eq-port.sh: plug -> the equalizer's
# ladspa PCM -> plug -> the mix) opened as SDL 2's ALSA backend opens it, at
# the rates and channels programs ask (tools/eqchain-try.c: your PICO-8's
# 22050 Hz mono, stereo, 44100, 48000, 32000, 11025), over a stand-in for the
# Pixel 2's dmix with its limits (tools/pcm-dmixlike.c: 48 kHz, stereo
# S16_LE, 512-frame periods, a buffer of 1024 to 2048 frames), on this
# computer's alsa-lib. First the system's default (plug into the mix, as
# every program but a port opens it), which says the stand-in holds, then
# the port's chain: every one must open. On 1.1.0-rc.5 PICO-8 had no sound,
# its 22050 Hz refused by the chain at the hardware parameters (EINVAL)
# where 44100 opened; this is that, on a computer. Then the volume boost
# through the same chain as PICO-8 plays into it (tools/eqchain-gain.c:
# 22050 Hz mono, the gain written into the equalizer's file half way, as
# portkeys writes it), its mix a file this time: the second half louder by
# the gain written. On 1.1.0-rc.8 the boost over PICO-8 was not heard.
#
#     sh tools/eqchain-check.sh            (check-eqplugin runs it on Linux)
#
# Needs Linux and alsa-lib's headers (pkg-config alsa); skips without them.
# tools/eqplugin-alsa.sh runs it in plastron's build image on the
# handheld's own alsa-lib: CC, SYSROOT_ALSA_CONF (alsa.conf) and RUN (what
# runs a program built with CC) come from there.
set -eu
REPO=$(cd "$(dirname "$0")/.." && pwd)
CC=${CC:-cc}
ALSA_CONF=${ALSA_CONF:-/usr/share/alsa/alsa.conf}
RUN=${RUN:-}
if [ -z "${EQCHAIN_SDK:-}" ]; then
	[ "$(uname -s)" = Linux ] || { echo "eqchain: skip, no ALSA on this computer"; exit 0; }
	pkg-config --exists alsa 2> /dev/null || { echo "eqchain: skip, no alsa-lib headers (pkg-config alsa)"; exit 0; }
	[ -f "$ALSA_CONF" ] || { echo "eqchain: skip, no $ALSA_CONF"; exit 0; }
fi
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
P=$REPO/plastron/buildroot/package/zyperos-eq/src
$CC -O2 -fPIC -shared -Wall -Wextra -I"$REPO/src" -o "$W/zyperos_eq.so" "$P/zyperos_eq.c" -lm
$CC -O2 -fPIC -DPIC -shared -Wall -Wextra -o "$W/libasound_module_pcm_dmixlike.so" "$REPO/tools/pcm-dmixlike.c" -lasound
$CC -O2 -Wall -Wextra -o "$W/try" "$REPO/tools/eqchain-try.c" -lasound
$CC -O2 -Wall -Wextra -I"$REPO/src" -o "$W/gain" "$REPO/tools/eqchain-gain.c" -lasound -lm
# The system's config, the Pixel 2's shape: its default plug into the mix,
# the mix the stand-in
mkdir -p "$W/xdg/alsa"
cat > "$W/xdg/alsa/asoundrc" <<CONF
pcm_type.dmixlike { lib "$W/libasound_module_pcm_dmixlike.so" }
pcm.!default { type plug; slave.pcm "mixed" }
pcm.mixed { type dmixlike }
CONF
sh "$REPO/sd/zyperos/eq-port.sh" mixed "$W/zyperos_eq.so" > "$W/port.conf"
sh "$REPO/sd/zyperos/eq-port.sh" --after "$W/port.conf" > "$W/port.hook"
fails=0
echo "eqchain: the system's default, plug into the mix (the stand-in holds):"
XDG_CONFIG_HOME=$W/xdg HOME=$W ALSA_CONFIG_PATH=$ALSA_CONF $RUN "$W/try" system 2> "$W/err" || fails=$((fails + 1))
echo "eqchain: a port's chain, sd/zyperos/eq-port.sh:"
XDG_CONFIG_HOME=$W/xdg HOME=$W ALSA_CONFIG_PATH=$ALSA_CONF:$W/port.hook $RUN "$W/try" port 2>> "$W/err" ||
	fails=$((fails + 1))
# The boost, the mix a file (a WAV of what the chain settled on): its own
# config, the same shape
mkdir -p "$W/gxdg/alsa"
cat > "$W/gxdg/alsa/asoundrc" <<CONF
pcm.!default { type plug; slave.pcm "mixed" }
pcm.mixed { type file; slave.pcm "null"; file "$W/gain.wav"; format "wav" }
CONF
sh "$REPO/sd/zyperos/eq-port.sh" mixed "$W/zyperos_eq.so" > "$W/gport.conf"
sh "$REPO/sd/zyperos/eq-port.sh" --after "$W/gport.conf" > "$W/gport.hook"
printf 'ZEQ1\002\000\000\000\000\000\000\000\000\000\000\000' > "$W/eq"
echo "eqchain: the volume boost through a port's chain, as portkeys writes it:"
XDG_CONFIG_HOME=$W/gxdg HOME=$W ZYPEROS_EQ_FILE=$W/eq ALSA_CONFIG_PATH=$ALSA_CONF:$W/gport.hook \
	$RUN "$W/gain" "$W/gain.wav" 6 2>> "$W/err" || fails=$((fails + 1))
if [ "$fails" != 0 ]; then
	sed 's/^/  alsa-lib: /' "$W/err" | sort -u | head -n 10
	echo "eqchain: FAILED"
	exit 1
fi
echo "ok: a port's chain opens at every rate a program asks, as SDL asks it, and carries the boost"
