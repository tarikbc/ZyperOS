#!/bin/sh
# Build DSperate, the Nintendo DS shelf's core.
#
# DSperate (github.com/beebono/DSperate, GPL-3.0) is an open DS emulator with
# an ARM64 JIT and NEON rendering, measured against melonDS. It has no
# libretro core of its own. dsperate-lr
# (github.com/thwonp/dsperate-lr, GPL-3.0) wraps its core library as one, and
# pins DSperate as a submodule. This builds the wrapper at its newest commit
# (27b9d76, which pins DSperate v3.3.0) with DSperate v3.4.1 checked out in
# place of its pin: v3.4.1's fixes are wanted (New Super Mario Bros. froze
# after the DS went to sleep, a white-screen boot hang on IPC FIFO timing, Zoo
# Keeper's PLD, Electroplankton's timer), and nothing in the wrapper needed
# changing for it. Until 2026-11-16 this was the commit plorpOS ships on its
# GKD and RG Rotate builds (thwonp/plorpOS mk/build-dsperate.sh, MIT), 093d50f
# with v3.0.5. The wrapper's own frame skip on the frontend's underrun hint
# (dsperate_frameskip, auto by default) is the change our first patch made,
# folded in upstream; it leaves out exactly the frames ours did for Diatom's
# frameskip=late and frameskip=steady (a frame-for-frame check, 2026-11-16).
# Plus eight patches of ours (below).
#
# PGO: DSperate commits a GCC profile with each release, made with GCC 13.3 on
# Ubuntu 24.04 (its CI), and the wrapper's build.sh applies it. A profile only
# applies to the compiler that made it, so the build runs in the wrapper's own
# Dockerfile, Ubuntu 24.04 with GCC 13.3 cross to aarch64. build.sh says how
# much of the profile applied; on 2026-11-03 it was "0 functions without their
# profile, 2 objects without one", which is what DSperate's CI reports too.
#
# x86, under emulation on an Arm Mac. Built that way, the unpatched wrapper at
# plorpOS's pin was plorpOS's file byte for byte (sha256 400ac7b4...2ca1), so
# the build was reproducible across both projects; it still builds the same
# bytes twice here. Built natively on arm64 it is not: Ubuntu's arm64 "cross" compiler is the native
# GCC under another name, needs the native binutils besides (the image has
# none, and `as` is missing), and makes different bytes (4394e1ec...). Same
# GCC version, same profile applied, same libraries; only x86 matches.
#
# Untuned ARMv8.0. plorpOS measured -mcpu on its devices, six games three runs
# each, and found no consistent gain (plorpos-46o.13); a Cortex-A35 is an
# in-order core like the A53 it measured, so nothing here argues for tuning
# before a measurement on the Pixel 2 does. DSPERATE_TUNE stays empty.
#
# The first patch, mk/patches/dsperate-lr-pgo-v341.patch: the wrapper's own
# patches/0001, which keeps DSPERATE_TUNE out of the PGO fingerprint, made
# against v3.4.1's CMakeLists.txt (v3.4.1 reworded the line it changes). The
# build is untuned, so the fingerprint is the same with it or without; it keeps
# the wrapper's set of fixes applying whole.
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
# adds the core option dsperate_language: firmware, the default and the old
# behaviour, or japanese, english, french, german, italian, spanish or
# chinese (the iQue DS's), which sets the language field of the user settings
# in the run's own copy of the firmware just before the game is started
# (DSperate's direct boot copies the settings from it), and again at every
# Reset, read afresh: a reset does not load the firmware again, so the copy
# kept the language already, but a game may write its own settings to the
# firmware while it runs, and the option may have changed. The launcher sends it
# with every DS game (src/dslang.h): the shelf's Game Language row, which is
# the language MENU > Language chose where the DS has it and English where it
# does not (Portuguese, Chinese) until it is set to one outright. Only that
# one field of the copy in memory
# changes: the dump on the card is not written (DSperate saves a changed
# firmware only from its own frontends, to a sidecar file, and this wrapper
# never does), and nothing of the game's save or of the other settings is
# touched. The wrapper reads its options after the game boots, so the
# language is read before (read_language), where the firmware's settings are
# made. Again only libretro.cpp changes, so the profile applies in full.
#
# The fourth, mk/patches/dsperate-lr-rtc.patch: the DS clock. DSperate keeps
# its RTC stopped unless the frontend starts it (Io::start_rtc_clock, "for
# determinism"), and the wrapper never did, so the clock stayed at
# 2000-01-01 00:00:00 and every game that shows the time showed 0:00
# (Pokemon Black, on my Pixel 2). The patch starts it at each game's boot,
# from the host's local time (localtime, so the zone Diatom sets from the
# launcher's before the game loads, as the GBA cores read it), and sets it to
# the host's time again after a state loads (a state brings back the time it
# was saved at), after a reset, and whenever it drifts a second or more from
# the host's, looked at before every frame: it counts emulated seconds, which
# stand still in the in-game menu and in sleep, run slow when frames run late
# and fast in fast forward. A real DS's clock keeps the time while no game runs, and
# DSperate's own frontend sets it again as the lid opens. Again only
# libretro.cpp changes, so the profile applies in full. mk/dsperate-rtc-check.sh
# reads the clock out of the built core's states (below).
#
# The fifth, mk/patches/dsperate-lr-ext-bg.patch: engine B's worker drew a
# rotated or scaled extended background (a bitmap, or a 16-bit tile map) with
# a lookup through the VRAM view's 16 KB blocks for every pixel, and in a
# Mario Kart DS race (its lower screen's map) that was most of the worker's
# time, while the emulation thread waited for it at line 0: 2.3 ms a frame on
# my Pixel 2 (2026-11-16, its threads sampled by their CPU time). The patch
# adds patches/0003-ext-bg-run.patch to the wrapper's own folder of DSperate
# fixes, which its build.sh applies: when every block of the bitmap, or of the
# map and the tileset, is backed by exactly one bank and the blocks lie back to
# back in host memory, each pixel is one load from that range; otherwise the
# lookup, as before. The wait went from 2.3 to 0.5 ms a frame, and the race's
# panel from 55 to 59 frames a second. It changes engine2d.cpp and
# vram_map.h, so one function loses its profile (the build log counts it).
# mk/dsperate-extbg-check.sh holds the built core's frames to the ones before
# the patch (below).
#
# The sixth, mk/patches/dsperate-lr-frame-direct.patch: the wrapper copied
# both screens into a frame of its own every retro_run (393 KB) and handed
# that to the frontend, which copies it again (Diatom, into the frame it
# presents). DSperate keeps the two screens as one array of two, the bottom
# one where the top one ends, which is the stacked frame already; the patch
# hands that over, and copies only if they ever stop being back to back. A
# frontend copies the frame within the call, and nothing draws into those
# buffers again until the next run_frame, whose drawing starts after its
# line-0 join. Only libretro.cpp changes. mk/dsperate-extbg-check.sh hashes
# the frames the core hands over, so it holds them to the same pixels.
#
# The seventh, mk/patches/dsperate-lr-bands.patch: since v3.2.0 (a6f2e935)
# DSperate's thread layout gives the 3D raster a third band, on the first
# core. Under the second patch that core is Diatom's: its present, audio and
# input threads run there at normal priority, and the band, at the workers'
# SCHED_RR, pre-empts them. On my Pixel 2, from the same saved states, the
# panel showed 56.6 fps in a Mario Kart DS race and 53.5 in Pokemon Black
# with three bands, 59.8 in both with two, at the same core time (11.3 ms a
# frame; 2026-11-16). The third band beside the 2D engines gained nothing,
# beside the emulation thread it cost Mario Kart 2.8 ms a frame. The patch
# adds patches/0004-two-bands-under-layout.patch to the wrapper's folder of
# DSperate fixes: two bands under the layout again, as up to v3.0.5
# (DS_R3D_BANDS=n still overrides). It changes one function of render3d.cpp,
# which loses its profile (the build log counts it).
#
# The eighth, mk/patches/dsperate-lr-zip-cache.patch: a zipped game is
# inflated once into .dsperate beside its zip and kept for the next launch
# (DSperate's cart/zip_cache.h), and DSperate's own frontend keeps that
# folder under cart.cache_mb, 2 GB, the least recently launched first. The
# wrapper never set NDS::rom_cache_max_bytes, so nothing ever went: every
# zipped DS game a player had played stayed at its unpacked size (asked for
# on Discord, 2026-11-23). The patch sets the same 2 GB, which the launcher
# also holds after each game (src/dscache.h), so the two agree. Only
# libretro.cpp changes. mk/dsperate-zipcache-check.sh loads a zip beside
# 2.5 GB of unpacked games and holds the core to it.
#
# The result is checked before it is installed: no unversioned undefined
# symbol (it would not dlopen), and nothing newer than plastron's glibc 2.42
# and libstdc++ 6.0.34 (GLIBCXX_3.4.34). It needs GLIBC_2.38 and GLIBCXX_3.4.30
# at most, and links libstdc++, libm, libgcc_s, libc and ld-linux, all in
# plastron's /usr/lib. Then mk/dsperate-rtc-check.sh runs it: the clock it
# keeps is the host's local time at a game's start, while it runs faster
# than real time, after a pause, a state load and a reset. And
# mk/dsperate-extbg-check.sh: 96 frames of extended backgrounds, each as the
# core drew it before the fifth patch. And mk/dsperate-state-check.sh: a save
# state the v3.0.5 core made (tools/dsperate-state-v305.state.gz) is taken,
# and the game runs on from it, which Diatom's STATE_READS rests on: the
# DS players' resume points made before the move to v3.4.1 still load.
# And mk/dsperate-zipcache-check.sh: the cap on its unpacked zips.
#
#   mk/build-dsperate.sh          build, install to vendor/cores
#
# Needs: docker with linux/amd64 emulation and arm64 (the clock check runs
# the core in ubuntu:24.04), network for the first clone. About four minutes
# on an M-series Mac.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${SRC:-/tmp/zyperos-dsperate-lr}
REPO=${DSPERATE_LR:-https://github.com/thwonp/dsperate-lr.git}
LR=27b9d76fb4c900d29f9629806f316527b5da90c2   # the wrapper's newest, which pins DSperate v3.3.0
DS=ecb27c96a80dd2c4bf31a0316f9eef9354d73e04   # DSperate v3.4.1, in place of that pin
SHA=0361a61a571ce61d34d3c8407cc71a6c288cf808c7bd9efc449d6ec03763093b
IMAGE=dsperate-lr-cross
PLAT=linux/amd64

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
[ -d "$SRC/.git" ] || git clone -q "$REPO" "$SRC"
git -C "$SRC" fetch -q origin 2>/dev/null || true
git -C "$SRC" checkout -q -f "$LR"
git -C "$SRC" submodule -q update --init --force
git -C "$SRC/DSperate" cat-file -e "$DS^{commit}" 2>/dev/null ||
	git -C "$SRC/DSperate" fetch -q origin "refs/tags/v3.4.1:refs/tags/v3.4.1"
git -C "$SRC/DSperate" checkout -q -f "$DS"
git -C "$SRC" submodule -q foreach git checkout -q -f .
git -C "$SRC" clean -qfdx
git -C "$SRC/DSperate" clean -qfdx
git -C "$SRC" apply "$ROOT/mk/patches/dsperate-lr-pgo-v341.patch"
git -C "$SRC" apply "$ROOT/mk/patches/dsperate-lr-threads.patch"
git -C "$SRC" apply "$ROOT/mk/patches/dsperate-lr-language.patch"
git -C "$SRC" apply "$ROOT/mk/patches/dsperate-lr-rtc.patch"
git -C "$SRC" apply "$ROOT/mk/patches/dsperate-lr-ext-bg.patch"
git -C "$SRC" apply "$ROOT/mk/patches/dsperate-lr-frame-direct.patch"
git -C "$SRC" apply "$ROOT/mk/patches/dsperate-lr-bands.patch"
git -C "$SRC" apply "$ROOT/mk/patches/dsperate-lr-zip-cache.patch"
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
# The clock a game reads, in the core just built (the fourth patch)
sh "$ROOT/mk/dsperate-rtc-check.sh" "$SO"
# Extended backgrounds drawn as before (the fifth)
sh "$ROOT/mk/dsperate-extbg-check.sh" "$SO"
sh "$ROOT/mk/dsperate-state-check.sh" "$SO"
# The 2 GB of unpacked zips (the eighth)
sh "$ROOT/mk/dsperate-zipcache-check.sh" "$SO"
GOT=$(shasum -a 256 "$SO" | cut -d' ' -f1)
if [ "$GOT" != "$SHA" ]; then
	echo "!! dsperate sha256 $GOT, expected $SHA" >&2
	echo "  The toolchain image, the source, the patch or the profile changed." >&2
	echo "  Verify the core, then update SHA here." >&2
	exit 1
fi
mkdir -p "$ROOT/vendor/cores"
cp "$SO" "$ROOT/vendor/cores/"
echo "  built  dsperate (dsperate-lr ${LR%${LR#???????}}, DSperate v3.4.1) + PGO + thread layout + language + clock + extended BG runs + direct frames + two 3D bands + zip cache cap, untuned ARMv8.0"
echo "  sha256 $GOT"
# Which patches are in it, for mk/payload.sh to hold it to: the 1.0.1 images
# shipped a core built before the language patch (mk/core-stamp.sh)
sh "$ROOT/mk/core-stamp.sh" write dsperate
