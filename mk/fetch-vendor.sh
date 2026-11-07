#!/bin/sh
# Populate vendor/cores with the libretro cores ZyperOS redistributes.
#
# That is the whole of it. ZyperOS carries no third-party runtime libraries;
# measurement says nothing needs them:
#
#   the cores need  libc libm librt libstdc++ libgcc_s ld-linux
#   zyperos.elf needs libSDL2 libSDL2_image libSDL2_ttf libm libdl libc
#
# Every one of those is in plastron's system, under /usr/lib. Settings are
# ZyperOS's own code in src/device/, against each device's ALSA controls and
# display.
#
# Cores come from libretro's own buildbot, pinned by sha256 - the same hashes
# Diatom's CORES.md verifies, because they are the same binaries. The
# buildbot path is unpinned; these hashes are the pin.
#
# Usage: mk/fetch-vendor.sh
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VENDOR=$ROOT/vendor
DL=${1:-/tmp/zyperos-vendor}
BB=https://buildbot.libretro.com/nightly/linux/aarch64/latest

command -v curl   > /dev/null || { echo "need curl" >&2; exit 1; }
command -v unzip  > /dev/null || { echo "need unzip" >&2; exit 1; }
command -v shasum > /dev/null || { echo "need shasum" >&2; exit 1; }

mkdir -p "$DL" "$VENDOR/cores"

fetch_core() { # name sha256
	if [ -f "$VENDOR/cores/${1}_libretro.so" ] &&
	   [ "$(shasum -a 256 "$VENDOR/cores/${1}_libretro.so" | cut -d' ' -f1)" = "$2" ]; then
		echo "  ok      $1"
		return
	fi
	curl -sSfL -o "$DL/$1.zip" "$BB/${1}_libretro.so.zip"
	unzip -o -j "$DL/$1.zip" -d "$VENDOR/cores" > /dev/null
	GOT=$(shasum -a 256 "$VENDOR/cores/${1}_libretro.so" | cut -d' ' -f1)
	if [ "$GOT" != "$2" ]; then
		echo "  MISMATCH $1" >&2
		echo "    want $2" >&2
		echo "    got  $GOT" >&2
		echo "  The buildbot republished this core. Verify it, then update the" >&2
		echo "  hash here and in Diatom's CORES.md together." >&2
		exit 1
	fi
	echo "  fetched $1"
}

fetch_core fceumm            1b13b00d4680394dad8000d5175f97be727107e0945bc9b412da91d70c07b267
fetch_core snes9x2010        3933890f520abb9dbb0e5276460785b20ce54d25f552b369cafeca270b9dd44c
# mgba IS NOT FETCHED, and that is the one exception here. Run
# mk/build-mgba-bridge.sh instead; it builds this exact commit plus upstream's
# own fix for a crash the buildbot core still has.
#
#   fetch_core mgba            abde7a0764f08fa0cc2c7d3d9a29b9d1245a9f3b7df0e7a594b74df642ee53c6
#
# Every MBC2 Game Boy cartridge segfaults on the buildbot core - Kirby's Pinball
# Land, Wave Race, Golf, X, both Final Fantasy Legends. Regression in mgba
# a1b2b23, reported as mgba-emu/mgba#3859, fixed upstream 543a1975 the same day,
# and in no downloadable core because libretro builds from its own fork which
# has not synced since 2026-08-06.
#
# RESTORE THE LINE ABOVE AND DELETE THE BRIDGE the day that fork syncs. The
# official binary will contain exactly the change we are carrying.
# mk/build-mgba-bridge.sh says what to re-verify when swapping back.
fetch_core genesis_plus_gx   3673a22b906509461e23a5a118b1d1bec15cbda105f260cbcbc08a16b2124e48
# Unlike the other binaries, Beetle PCE Fast's carries no commit. Its source is
# beetle-pce-fast-libretro bebe2b13a840fbfd45cb501f5cd0efe01bdb1735: built from
# that commit with the buildbot's setup (Ubuntu 20.04, GCC
# 12.5.0-8ubuntu2~20~ppa3 from ppa:ubuntu-toolchain-r/test, make platform=unix,
# strip), it gives this exact file, byte for byte (checked 2026-10-25).
fetch_core mednafen_pce_fast aca90a14b18108c86398da2267ef40d5145eaddbc1c1b310614d745b258552b1
fetch_core mednafen_ngp      a2015668f9a9403b8bf6941b550fae2c618f37b79173e8ba27c95b95f96bdd99
# The four small systems, pinned 2026-11-03 from the buildbot's latest that
# day. Like mednafen_ngp they are pinned here and not yet in Diatom's CORES.md,
# whose core-facts.md wants a device measurement first. Each was run in a
# linux/arm64 container under Diatom's desktop port, not yet on a device:
#
#   stella2014  Stella 2014 3.9.3 7d1361e           River Raid, Pitfall!
#   prosystem   ProSystem 1.3e 8a88014              Klax, with and without BIOS
#   gw          Game & Watch 1.6.3 (gw dddc9d55)    Monkey (VTech)
#   fbneo       FinalBurn Neo v1.0.0.03 GITda14132  ssf2, 1941, sfiii
#
# FinalBurn Neo is 80 MB on disk, and what it costs resident is not the file:
# 15 MB of its data is written by the dynamic linker at dlopen, and running a
# CPS2 game left about 3 MB more behind after the game was unloaded. The
# other three keep 0.5 to 1.1 MB each, except gw, which keeps about 2 MB more
# after every game it unloads - 35 MB after sixteen loads in one process.
# Measured with a harness that dlopens each core and reads
# /proc/self/smaps_rollup, in the same container.
fetch_core stella2014        b237ddd54c534558552ff5951be46964a4b5fa9f32df87888e180757cb96a2d0
fetch_core prosystem         284a5c1390d47c00967bed8c8b83a14a04234508fff69ffac906d532f7db8a51
fetch_core gw                56bc41f0e49565ed84d35e7a92949bffa075b4ab6be0f0dc84118ab4fd16b815
fetch_core fbneo             f3140935e6c5396675b542b3396a9d107f5453d22df658f067ac1866f94e7616
# Sega CD, 32X and Neo Geo CD, pinned 2026-11-03 from the buildbot's latest
# that day, and like the four above run only in a linux/arm64 container under
# Diatom's desktop port so far. The Neo Geo shelf is fbneo's, above.
#
#   picodrive  PicoDrive 2.05-1890c29   Virtua Racing Deluxe, Knuckles'
#                                       Chaotix, Star Wars Arcade (32X);
#                                       Sonic CD, Popful Mail (Sega CD)
#   neocd      NeoCD 2022 b1e04c7       Metal Slug, to its menu
#
# CPU time a frame in that container, the same for every core so only the
# ratios mean anything: genesis_plus_gx running Sonic 2 0.32 to 0.34 ms,
# PicoDrive 0.36 to 0.48 for the 32X and 0.25 to 0.38 for the Sega CD, NeoCD
# 0.27 to 0.47, FBNeo 0.30 to 0.48 on Neo Geo sets. Each holds 60 fps there.
#
# Resident cost, Anonymous in /proc/self/smaps_rollup with the core loaded,
# its retro_init run once and a game loaded and unloaded the way Diatom does
# it: PicoDrive 2.3 MB idle, about 8 MB after a 32X game (the ROM and the
# 32X's memory stay mapped until the next load, and the SH-2 recompiler's
# tables until a non-32X one) and 5.5 MB after a Sega CD game; NeoCD 9 MB
# idle and 11.5 MB after a game. Neither grew over repeated loads, ten of
# PicoDrive's and six of NeoCD's in one process.
#
# picodrive's sha256 is the one plorpOS pins for the same build.
fetch_core picodrive         d0956ac7138ba4f8e5e849d4bd8c544f27108c17a03ea4944e56cc04a9c8028e
fetch_core neocd             684caf31de3f1efe3c7fa583d0d5db61ce00097c6e8311fa34d3069a0ddd17fa
# fake08, for PICO-8, IS NOT FETCHED either: the buildbot has no aarch64 build
# of it, and Diatom needs three patches to it. Run mk/build-fake08.sh, which
# builds and pins it.

# The PlayStation. PCSX-ReARMed r26l-137-g7b0e8f8, libretro/pcsx_rearmed
# 7b0e8f8, pinned 2026-11-03 with Diatom's CORES.md. Wants a BIOS in Bios/,
# scph5501.bin for US discs, which systems.cfg names as the shelf's disc bios.
fetch_core pcsx_rearmed      07cb263f1af645ea60906dbdc8d00ba32cd735bde60cf7d8fbc210118e169808

# DSperate, the Nintendo DS shelf's libretro core, is not on the buildbot at
# all: mk/build-dsperate.sh builds it, pinned by sha256 like these.

# flycast2021, the Dreamcast's, is not on the buildbot for aarch64 either, and
# needs one patch to link: mk/build-flycast2021.sh builds and pins it.

# ROGUE OS's other systems, pinned 2026-11-04 from the buildbot's latest that
# day, each on the core ROGUE ran it with (the default in its es_systems.cfg):
#
#   a5200           Atari 5200            a5200 2.0.2 40c6f2f
#   handy           Lynx                  Handy 0.97 bc55d46
#   mednafen_wswan  WonderSwan and Color  Beetle WonderSwan v0.9.35.1 4b01295
#   pokemini        Pokemon Mini          PokeMini v0.60
#   bluemsx         MSX2                  blueMSX e3086eb
#   opera           3DO                   Opera 1.0.0 370d9a2
#   mame2003_plus   MAME                  MAME 2003-Plus 91f2f15
#
# NAOMI and Atomiswave run on flycast2021, above. Virtual Boy (mednafen_vb),
# Vectrex (vecx) and Nintendo 64 (mupen64plus_next) are built, not fetched:
# the buildbot's builds of those link desktop OpenGL, which the devices do not
# have (mk/build-small-cores.sh, mk/build-mupen64plus-next.sh).
fetch_core a5200             7998a4b8c9592efd29502da9f9739e4cbcc58e3ffd6ca2495ca46fe78f630c11
fetch_core handy             3efb38b73e44b561d89f4877e9ac86a1fe99bcb23a1ad0843204d2dd254d892e
fetch_core mednafen_wswan    700b6eb5678a3283db9818cf22c68783d0d7f1607142b1d069c366eab81dfbaa
fetch_core pokemini          38b87e0912697ce6c2a1fde11c2c12ef2d807e2e26c462f4c64098fe236efa98
fetch_core bluemsx           cb6a7d33d93a23e4089f5cf3d44406b6e45dadc2d0ba429fa7157012d2ac301b
fetch_core opera             6d849ed4e515663601a4518f1dbe66a9af9cb82f9e8d7a7bcf73042d8e0947f2
fetch_core mame2003_plus     81c74e5f1497ba2380706112675c122f8d909272bb99540e0752fe8e2e86a269

echo "vendor/cores ready:"
ls "$VENDOR/cores"
