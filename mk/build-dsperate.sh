#!/bin/sh
# Build DSperate, the Nintendo DS core the Nintendo DS shelf can run instead
# of DraStic (its Core row, src/main.c).
#
# DSperate (github.com/beebono/DSperate, GPL-3.0) is an open DS emulator that
# reimplements DraStic's techniques - an ARM64 JIT and NEON rendering - written
# from DraStic's freely distributed debug-symbol build and measured against
# melonDS. It has no libretro core of its own. dsperate-lr
# (github.com/thwonp/dsperate-lr, GPL-3.0) wraps its core library as one, and
# pins DSperate as a submodule, so one commit of the wrapper pins both. This is
# the wrapper plorpOS ships on its GKD and RG Rotate builds
# (thwonp/plorpOS mk/build-dsperate.sh, MIT), at the same commit, built the same
# way, plus three patches of ours (below).
#
# PGO: DSperate commits a GCC profile with each release, made with GCC 13.3 on
# Ubuntu 24.04 (its CI), and the wrapper's build.sh applies it. A profile only
# applies to the compiler that made it, so the build runs in the wrapper's own
# Dockerfile, Ubuntu 24.04 with GCC 13.3 cross to aarch64. build.sh says how
# much of the profile applied; on 2026-11-03 it was "0 functions without their
# profile, 2 objects without one", which is what DSperate's CI reports too.
#
# x86, under emulation on an Arm Mac. Built that way the unpatched wrapper is
# plorpOS's file byte for byte (sha256 400ac7b4...2ca1, 3412064 bytes, the hash
# its script pins), so the build is reproducible across both projects. Built
# natively on arm64 it is not: Ubuntu's arm64 "cross" compiler is the native
# GCC under another name, needs the native binutils besides (the image has
# none, and `as` is missing), and makes different bytes (4394e1ec...). Same
# GCC version, same profile applied, same libraries; only x86 matches.
#
# Untuned ARMv8.0. plorpOS measured -mcpu on its devices, six games three runs
# each, and found no consistent gain (plorpos-46o.13); a Cortex-A35 is an
# in-order core like the A53 it measured, so nothing here argues for tuning
# before a measurement on the Pixel 2 does. DSPERATE_TUNE stays empty.
#
# The first patch, mk/patches/dsperate-lr-frameskip.patch: the frontend's "this
# frame will run late" makes the GPU leave that frame's drawing out. Diatom
# already says it for fast forward, through the audio buffer status callback,
# and for a DS game it says it at 1x too when a frame runs late (RUN
# frameskip=late, src/main.c in Diatom). The callback is handed over in
# retro_load_game, not retro_init: Diatom keeps a core loaded across games and
# forgets the last game's callback at each one, and retro_init runs once.
# Only the wrapper's libretro.cpp changes, which the profile does not cover, so
# the profile still applies in full. The idea is nelynes's slotPlus
# (crates/slot-retro); plorpOS carries a change like it for its Brick build.
#
# The second, mk/patches/dsperate-lr-threads.patch: DSperate's own thread
# layout, which its own frontend runs with and the wrapper left off. For each
# game the frontend's emulation thread (the one calling retro_run) gets a core
# of its own, each 2D engine a worker beside a 3D band on the next two cores,
# and the frontend's other threads the first core, with the emulation thread
# and the video workers at SCHED_RR 5; all of it is put back as the game
# unloads, since Diatom keeps the core loaded and its threads across games.
# Measured on the Pixel 2, Mario Kart DS, a Grand Prix race, 30 s: 16.7 ms a
# drawn frame, 20% of the frames undrawn and 49 fps at the panel without it;
# 14.0 ms, 2% and 59.6 fps with it, Diatom holding the CPU at its top clock
# (the layout without real-time: 16%; real-time without the layout: 18%).
# Again only the wrapper's libretro.cpp changes, so the profile applies in full.
#
# The third, mk/patches/dsperate-lr-language.patch: the language a game sees.
# A DS game reads it from the firmware's user settings, and a firmware.bin in
# Bios/ carries the language of the console it was dumped from: the one on my
# ROGUE card, which tools/from-rogue.sh copies to Bios/, is Spanish (language
# 5), and Mario Kart DS came up in Spanish on my Pixel 2 whatever I or the
# handheld speak. Nothing in the wrapper or in Diatom looked at it. The patch
# adds the core option dsperate_language (restart): firmware, the default and
# the old behaviour, or japanese, english, french, german, italian, spanish or
# chinese (the iQue DS's), which sets the language field of the user settings
# in the run's own copy of the firmware just before the game is started
# (DSperate's direct boot copies the settings from it). The launcher sends it
# with every DS game (src/dslang.h): the shelf's Game Language row, which is
# the language MENU > Language chose where the DS has it and English where it
# does not (Portuguese, Chinese) until it is set to one outright. Only that
# one field of the copy in memory
# changes: the dump on the card is not written (DSperate saves a changed
# firmware only from its own frontends, to a sidecar file, and this wrapper
# never does), and nothing of the game's save or of the other settings is
# touched. Again only libretro.cpp changes, so the profile applies in full.
#
# The result is checked before it is installed: no unversioned undefined
# symbol (it would not dlopen), and nothing newer than plastron's glibc 2.42
# and libstdc++ 6.0.34 (GLIBCXX_3.4.34). It needs GLIBC_2.38 and GLIBCXX_3.4.30
# at most, and links libstdc++, libm, libgcc_s, libc and ld-linux, all in
# plastron's /usr/lib.
#
#   mk/build-dsperate.sh          build, install to vendor/cores
#
# Needs: docker with linux/amd64 emulation, network for the first clone. About
# four minutes on an M-series Mac.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${SRC:-/tmp/zyperos-dsperate-lr}
REPO=${DSPERATE_LR:-https://github.com/thwonp/dsperate-lr.git}
LR=093d50fcbd10531273e0cc22a23c6644a26b9b71   # DSperate v3.0.5 (ddf2910), plorpOS's pin
SHA=570f7a59191cb38dc40b8abbdee19bba35c03810cbb1af0cb14f240e89b5ec01
IMAGE=dsperate-lr-cross
PLAT=linux/amd64

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
[ -d "$SRC/.git" ] || git clone -q "$REPO" "$SRC"
git -C "$SRC" fetch -q origin 2>/dev/null || true
git -C "$SRC" checkout -q -f "$LR"
git -C "$SRC" submodule -q update --init --force
git -C "$SRC" submodule -q foreach git checkout -q -f .
git -C "$SRC" clean -qfdx
git -C "$SRC/DSperate" clean -qfdx
git -C "$SRC" apply "$ROOT/mk/patches/dsperate-lr-frameskip.patch"
git -C "$SRC" apply "$ROOT/mk/patches/dsperate-lr-threads.patch"
git -C "$SRC" apply "$ROOT/mk/patches/dsperate-lr-language.patch"
docker image inspect $IMAGE >/dev/null 2>&1 ||
	docker build --platform $PLAT -q -t $IMAGE "$SRC" >/dev/null

docker run --rm --platform $PLAT -e DSPERATE_TUNE= -v "$SRC:/w" -w /w $IMAGE sh -c '
set -e
./build.sh /w/build | grep -v "^\["
so=/w/build/dsperate_libretro.so
[ "$(aarch64-linux-gnu-nm -D -u $so | grep -v " w " | grep -vc @)" = 0 ] \
  || { echo "!! undefined symbols - the core will not dlopen" >&2; exit 1; }
g=$(aarch64-linux-gnu-objdump -T $so | grep -oE "GLIBC_2\.[0-9]+" | sort -uV | tail -1)
x=$(aarch64-linux-gnu-objdump -T $so | grep -oE "GLIBCXX_3\.4\.[0-9]+" | sort -uV | tail -1)
echo "  needs  $g, $x"
[ "${g#GLIBC_2.}" -le 42 ] && [ "${x#GLIBCXX_3.4.}" -le 34 ] \
  || { echo "!! newer than plastron (glibc 2.42, GLIBCXX_3.4.34)" >&2; exit 1; }
' 2>&1 | grep -v 'warning\|note:\|inlined from\|In function\|In member\|^ *[0-9]* |\|^ *|\|^/w/\|^$' || true

SO=$SRC/build/dsperate_libretro.so
[ -f "$SO" ] || { echo "!! build failed (see $SRC/build/*.log)" >&2; exit 1; }
GOT=$(shasum -a 256 "$SO" | cut -d' ' -f1)
if [ "$GOT" != "$SHA" ]; then
	echo "!! dsperate sha256 $GOT, expected $SHA" >&2
	echo "  The toolchain image, the source, the patch or the profile changed." >&2
	echo "  Verify the core, then update SHA here and in the README together." >&2
	exit 1
fi
mkdir -p "$ROOT/vendor/cores"
cp "$SO" "$ROOT/vendor/cores/"
echo "  built  dsperate (dsperate-lr ${LR%${LR#???????}}, DSperate v3.0.5) + PGO + frameskip + thread layout + language, untuned ARMv8.0"
echo "  sha256 $GOT"
