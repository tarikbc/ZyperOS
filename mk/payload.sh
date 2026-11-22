#!/bin/sh
# Assemble ZyperOS's card payload: under out/sd/ for the TrimUI Brick, or
# out/sd-pixel2/ with DEVICE=pixel2 for the GKD Pixel 2. plastron, the system
# on both, builds it into its card image and copies it onto the card on first
# boot, so its build sets where the binaries are and where the payload goes:
#
#   DEVICE=brick ZYPEROS_ELF=... MP4D_ELF=... DIATOM_ELF=... VENDOR_DIR=... \
#       BTPLAYER_ELF=... OUT=... mk/payload.sh
#
# BTPLAYER_ELF only for a device with Bluetooth: tools/btplayer.c, which
# plastron's /etc/init.d/bluetooth runs from the card.
#
# P2D_ELF for the Pixel 2, and needed there: p2d (src/p2d/), NES Player 2's
# server, which the launcher starts from beside itself on the card. The
# Pixel 2 offers Player 2 in an NES game's menu, and without p2d the row
# would start nothing but an error.
#
# P8FX_SO for the Pixel 2, and needed there too: p8fx.so (src/p8fx/), the
# library your PICO-8's script loads before SDL for the PICO-8 shelf's
# Shader and Border (src/p8fx.h). Without it those rows would change nothing
# over your PICO-8, with nothing to say why.
#
# TrimUI's own firmware is no longer supported, so there is one Brick payload,
# plastron's. NATIVE=1, which used to choose it, is accepted and ignored. And
# no release zip: it was the Brick's install for TrimUI's firmware, and both
# devices now install from plastron's card image. ZIP= is ignored too.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEVICE=${DEVICE:-brick}
# The device's build (config/device/<id>.mk, from src/device/<id>_build.c):
# its Diatom port and the helpers its payload carries beside the launcher.
# The card script is one for every device (sd/zyperos/launch.sh); what a
# device has of its own is its device.sh, which launch.sh sources
DEVFILE=$ROOT/config/device/$DEVICE.mk
DEVICE_SH=$ROOT/sd/$DEVICE/device.sh
[ -f "$DEVFILE" ] && [ -f "$DEVICE_SH" ] ||
	{ echo "payload: unknown DEVICE $DEVICE (no config/device/$DEVICE.mk or sd/$DEVICE/device.sh)" >&2; exit 1; }
DIATOM_PORT=$(sed -n 's/^ZYDEV_DIATOM_PORT = *//p' "$DEVFILE")
HELPERS=" $(sed -n 's/^ZYDEV_HELPERS = *//p' "$DEVFILE") "
has_helper() { case $HELPERS in *" $1 "*) return 0 ;; esac; return 1; }
if [ "$DEVICE" = brick ]; then OUT=${OUT:-$ROOT/out/sd}; else OUT=${OUT:-$ROOT/out/sd-$DEVICE}; fi
P=$OUT/ZyperOS
ZYPEROS_ELF=${ZYPEROS_ELF:-$ROOT/build/zyperos.elf}
MP4D_ELF=${MP4D_ELF:-$ROOT/build/mp4d}
VENDOR_DIR=${VENDOR_DIR:-$ROOT/vendor}

[ -f "$ZYPEROS_ELF" ] || { echo "run make first"; exit 1; }
DIATOM_ELF=${DIATOM_ELF:-$ROOT/diatom/build/$DIATOM_PORT/diatom}
[ -f "$DIATOM_ELF" ] || { echo "no diatom at $DIATOM_ELF (set DIATOM_ELF)"; exit 1; }
[ -f "$VENDOR_DIR/cores/fceumm_libretro.so" ] || { echo "run mk/fetch-vendor.sh first"; exit 1; }
if has_helper p2d && [ ! -f "${P2D_ELF:-}" ]; then
	echo "payload: no p2d for $DEVICE's Player 2 (set P2D_ELF)" >&2
	exit 1
fi
if has_helper p8fx && [ ! -f "${P8FX_SO:-}" ]; then
	echo "payload: no p8fx.so for $DEVICE's PICO-8 Shader and Border (set P8FX_SO)" >&2
	exit 1
fi

rm -rf "$OUT"
mkdir -p "$P/system-icons" "$P/cores" "$P/res/web" \
         "$OUT/Roms" "$OUT/Music" "$OUT/Audiobooks" "$OUT/Bios" "$OUT/Saves" \
         "$OUT/Menu Music"

cp "$ZYPEROS_ELF" "$P/zyperos.elf"
cp "$MP4D_ELF" "$P/mp4d"                  # the audio player's engine; MP4 cannot play without it
[ -n "$BTPLAYER_ELF" ] && cp "$BTPLAYER_ELF" "$P/btplayer"   # a headset's keys and volume
[ -n "${PORTKEYS_ELF:-}" ] && cp "$PORTKEYS_ELF" "$P/portkeys" # the volume keys during a port (Pixel 2)
[ -n "${ZYHOME_ELF:-}" ] && cp "$ZYHOME_ELF" "$P/zyhome"     # MENU+START, back to the home (Pixel 2)
[ -n "${UPDATE_ELF:-}" ] && cp "$UPDATE_ELF" "$P/zyperos-update"  # System Update (update/, docs/updates.md)
# NES Player 2's server, where the launcher looks for it: beside itself, at
# P_ROOT/p2d (src/main.c's p2_setup). It serves the page from res/web/play,
# below.
[ -n "${P2D_ELF:-}" ] && cp "$P2D_ELF" "$P/p2d"
# The card's scripts: launch.sh, eq-port.sh, the device's device.sh and the
# helpers' own (OpenBOR's, your PICO-8's, the headsets' ALSA config), one
# list with make px2-programs (mk/card-scripts.sh)
DEVICE=$DEVICE sh "$ROOT/mk/card-scripts.sh" "$P"
# Which ZyperOS this is, for plastron: the Pixel's image copies its ZyperOS/ over
# the card's only when it is newer than the card's (/etc/init.d/zyperos), so a
# system-only update brings the new ZyperOS with it. Only when the build says:
# a payload built without a version has no file, and is never copied over one.
if [ -n "${VERSION:-}" ]; then
	echo "$VERSION" > "$P/VERSION"
fi
# Only systems.cfg is shipped now. zyperos.cfg, turbo.cfg and coreopts.cfg are
# compiled into the launcher and seed the settings database on first run, so
# there is no file to ship and none to drift from the code that reads it.
#
# systems.cfg stays a file because it is BUILD input, not a setting: the check
# below reads it to refuse a card whose cores are missing, and the loop further
# down reads it to create the ROM folders. Both run on the host, before any
# database exists.
cp "$ROOT/config/systems.cfg" "$P/"
# Which core options Core Settings shows (empty / absent = all); src/coreopt_defs.c
cp "$ROOT/config/coreopts-show.cfg" "$P/coreopts-show.cfg"
cp "$ROOT/res/system-icons/"*.png "$P/system-icons/" # pixel home atlases and stills
cp "$ROOT/res/fonts/menu.ttf" "$P/"       # the UI face, and the in-game menu's
cp "$ROOT/res/fonts/title.ttf" "$P/"      # its SemiBold, the title role's, found beside it
cp "$ROOT/res/fonts/zh-menu.otf" "$P/"    # 简体中文's faces and 日本語's, beside
cp "$ROOT/res/fonts/zh-title.otf" "$P/"   # them (tools/cjkfont.py), opened only
cp "$ROOT/res/fonts/ja-menu.otf" "$P/"    # when Chinese or Japanese is drawn
cp "$ROOT/res/fonts/ja-title.otf" "$P/"
cp "$ROOT/res/singles.png" "$P/"          # MP4's cover for Singles, the loose songs
cp "$ROOT/res/zyper.png" "$P/"            # Zyper's photo, on About's The name page
# The interface sounds, every voice (src/uisound.h): the launcher reads them
# from res/sounds/<voice>/ beside res/web/, and plays nothing without them.
# The WAVs only, so a .DS_Store left in the folder does not ship.
for v in "$ROOT"/res/sounds/*/; do
	v=$(basename "$v")
	mkdir -p "$P/res/sounds/$v"
	cp "$ROOT/res/sounds/$v/"*.wav "$P/res/sounds/$v/"
done
# Menu music for a card with none of its own in Menu Music (src/bgm.h): ZyperOS's few
# free tracks, credited in THIRD-PARTY-LICENSES.md, which goes to Licenses/
# below. The OGGs only, so a .DS_Store left in the folder does not ship;
# `make check-bgm-tracks` holds them to their licenses and their size cap.
mkdir -p "$P/res/bgm"
cp "$ROOT/res/bgm/"*.mp3 "$P/res/bgm/"
# The tutorial's home screen previews (DESIGN.md 11.10, "The choices"): the
# Theme and Home Screen steps read them from res/tour/, and show empty frames
# without them.
mkdir -p "$P/res/tour"
cp "$ROOT/res/tour/"*.png "$P/res/tour/"
# The Shader and Strength lists' pictures (src/shaderdemo.h), made by
# tools/shaderdemo.c for each panel: the device's own panel's alone. The
# Pixel 2's 640x480, the Brick's 1024x768.
if [ "$DEVICE" = pixel2 ]; then DEMO=640x480; else DEMO=1024x768; fi
mkdir -p "$P/res/shaderdemo/$DEMO"
cp "$ROOT/res/shaderdemo/$DEMO/"*.png "$P/res/shaderdemo/$DEMO/"
# Wi-Fi File Transfer's page. The launcher serves these off the card at P_WEB, so a
# payload without them is a card whose transfer screen starts a server and then
# answers its own page with a 404, which only shows on a built card - the
# reason to build one before calling it a release. The fonts go in twice
# rather than being kept in the repo twice: the launcher reads $P/menu.ttf and
# $P/title.ttf, and a browser asks for /web/menu.ttf and /web/title.ttf (the
# page's 600, which a browser would otherwise make up by smearing Regular).
# -R for i18n/, the page's words a language (tools/i18n.py), which the
# launcher answers /i18n.js with in the language that is on.
cp -R "$ROOT/res/web/"* "$P/res/web/"
cp "$ROOT/res/fonts/menu.ttf" "$P/res/web/menu.ttf"
cp "$ROOT/res/fonts/title.ttf" "$P/res/web/title.ttf"
# NES Player 2's play page came along in res/web/play, with its words in
# res/web/i18n (p2d answers /i18n.js from there). p2d serves nothing else
# outside play/, so the page's faces go in a third time, beside it.
cp "$ROOT/res/fonts/menu.ttf" "$P/res/web/play/menu.ttf"
cp "$ROOT/res/fonts/title.ttf" "$P/res/web/play/title.ttf"
# The device has curl and OpenSSL but nothing to trust - see res/ssl/README.md.
# Without this, every HTTPS request fails verification and achievements never
# arrive, with an error that reads like the network being down.
cp "$ROOT/res/ssl/cacert.pem" "$P/"
# Licenses/ on the card: ZyperOS's license and notice, Eric Reinsmidt's MIT
# notices, and the notices for everything else the card carries. plastron
# adds its own and Diatom's to the same folder.
mkdir -p "$OUT/Licenses"
cp "$ROOT/THIRD-PARTY-LICENSES.md" "$OUT/Licenses/ZyperOS.md"
cp "$ROOT/LICENSE" "$OUT/Licenses/ZyperOS-LICENSE.md"
cp "$ROOT/NOTICE" "$OUT/Licenses/ZyperOS-NOTICE.txt"
cp "$ROOT/TRADEMARK.md" "$OUT/Licenses/ZyperOS-TRADEMARK.md"
cp "$ROOT/LICENSES/MIT-TortOS.txt" "$OUT/Licenses/"
# ...and the licenses of the menu music's tracks, which it names
for f in "$ROOT"/res/bgm/LICENSE-*.txt; do
	cp "$f" "$OUT/Licenses/menu-music-${f##*/LICENSE-}"
done
cp "$DIATOM_ELF" "$P/diatom"
# A core ZyperOS patches goes on the card only as the build of the patches in
# mk/patches now: the 1.0.1 images carried a Nintendo DS core built before its
# language patch, and the Game Language row did nothing (mk/core-stamp.sh)
sh "$ROOT/mk/core-stamp.sh" check "$VENDOR_DIR" ||
	{ echo "payload: a patched core in $VENDOR_DIR is stale or unstamped" >&2; exit 1; }
cp "$VENDOR_DIR/cores/"*.so "$P/cores/"
# ScummVM's engine data and GUI theme, from mk/build-scummvm.sh with its core.
# The core reads them from Bios/scummvm, which launch.sh fills from here: a
# payload cannot write Bios/ over a player's card, and a ZyperOS update brings
# them along with the core they belong to.
if [ -f "$P/cores/scummvm_libretro.so" ]; then
	[ -f "$VENDOR_DIR/scummvm/.pin" ] ||
		{ echo "payload: scummvm's data is missing; run mk/build-scummvm.sh" >&2; exit 1; }
	cp -R "$VENDOR_DIR/scummvm" "$P/scummvm"
fi

# PrBoom's base settings (config/prboom.cfg): launch.sh copies them to
# Bios/prboom/prboom.cfg, where the Doom shelf's core looks for a base
# config, whenever they change.
mkdir -p "$P/prboom"
cp "$ROOT/config/prboom.cfg" "$P/prboom/prboom.cfg"

# PPSSPP's assets, from mk/build-ppsspp.sh with its core: the PSP's fonts,
# its VFPU tables, the game fixes and the system dialogs' languages. The core
# reads them from Bios/PPSSPP, which launch.sh fills from here, as it fills
# Bios/scummvm.
if [ -f "$P/cores/ppsspp_libretro.so" ]; then
	[ -f "$VENDOR_DIR/ppsspp/.pin" ] ||
		{ echo "payload: PPSSPP's assets are missing; run mk/build-ppsspp.sh" >&2; exit 1; }
	cp -R "$VENDOR_DIR/ppsspp" "$P/ppsspp"
fi

# FreeJ2ME-Plus's Java app and the Java runtime it starts that app with,
# from mk/build-freej2me.sh with its core. The core reads them from Bios/,
# which launch.sh fills from here: a payload cannot write Bios/ over a
# player's card, and a ZyperOS update brings them along with their core.
if [ -f "$P/cores/freej2me_plus_libretro.so" ]; then
	[ -f "$VENDOR_DIR/freej2me/.pin" ] ||
		{ echo "payload: FreeJ2ME-Plus's app and runtime are missing; run mk/build-freej2me.sh" >&2; exit 1; }
	mkdir -p "$P/freej2me"
	cp "$VENDOR_DIR/freej2me/freej2me_plus-lr.jar" \
	   "$VENDOR_DIR/freej2me/freej2me_plus_runtime_linux-arm64.tar.gz" \
	   "$VENDOR_DIR/freej2me/.pin" "$P/freej2me/"
fi

# Every system on the shelf must have its core on the card.
#
# The copy above is a glob: it ships whatever vendor/cores happens to hold and
# says nothing about what is missing. That is how a card was nearly built with
# four of nine systems dead - systems.cfg gained SNES and the Sega machines,
# vendor/ still held the original three, and nothing failed. A shelf whose
# cards open onto nothing is a worse failure than a build that refuses.
missing=
# "port" and "run:<script>" are not cores: those shelves hand their games to
# launch.sh, which runs a script instead of Diatom. A shelf can list several
# cores, comma separated (the Dreamcast: flycast2021 and Flycast 2.7's
# script), and each libretro one of them must be here.
for core in $(awk -F'|' '$1=="sys"{n=split($4, c, ","); for (i = 1; i <= n; i++) {
                  gsub(/^[ \t]+|[ \t]+$/,"",c[i]); if (c[i] != "port" && c[i] !~ /^run:/) print c[i] } }' \
              "$ROOT/config/systems.cfg" | sort -u); do
	[ -f "$P/cores/${core}_libretro.so" ] || missing="$missing $core"
done
if [ -n "$missing" ]; then
	echo "payload: systems.cfg needs cores that vendor/ does not have:$missing" >&2
	echo "payload: run mk/fetch-vendor.sh, then the builds vendor/ does not yet have:" >&2
	echo "payload: mk/build-mgba-bridge.sh, mk/build-dsperate.sh, mk/build-fake08.sh," >&2
	echo "payload: mk/build-flycast2021.sh, mk/build-scummvm.sh, mk/build-freej2me.sh," >&2
	echo "payload: mk/build-small-cores.sh, mk/build-ppsspp.sh, mk/build-tic80.sh," >&2
	echo "payload: mk/build-pcsx-rearmed.sh, mk/build-mupen64plus-next.sh, mk/build-parallel-n64.sh," >&2
	echo "payload: mk/build-yabasanshiro.sh (and mk/build-openbor.sh for the Pixel 2)" >&2
	# The small cores have one command for all of them; tools/release.sh,
	# tools/px2-payload.sh and plastron's card builds run it before this
	small=
	for core in $missing; do
		grep -q "^$core|" "$ROOT/mk/build-small-cores.sh" && small="$small $core"
	done
	if [ -n "$small" ]; then
		echo "payload:$small: in the main checkout (or with ZYPEROS_VENDOR set to the vendor/ this build reads), run" >&2
		echo "payload:   sh mk/build-small-cores.sh --missing" >&2
	fi
	exit 1
fi

# And no more than the resident Diatom keeps open. It premaps every core in
# the folder, in folder order, and refuses the ones past DIATOM_MAX_RESIDENT:
# 1.0.5's card had 34 for 32, and Vectrex and Saturn ran every game in the
# launcher's one-off Diatom (tools/core-room-check.sh checks systems.cfg).
room=$(awk '$1 == "#define" && $2 == "DIATOM_MAX_RESIDENT" { print $3 }' "$ROOT/diatom/src/diatom.h")
ncores=$(ls "$P/cores" | grep -c '_libretro\.so$')
if [ -z "$room" ] || [ "$ncores" -gt "$room" ]; then
	echo "payload: $ncores cores, and Diatom keeps ${room:-?} open (DIATOM_MAX_RESIDENT in diatom/src/diatom.h)" >&2
	exit 1
fi

chmod +x "$P/launch.sh" "$P/zyperos.elf" "$P/diatom" "$P/mp4d"
[ -f "$P/btplayer" ] && chmod +x "$P/btplayer"
[ -f "$P/portkeys" ] && chmod +x "$P/portkeys"
[ -f "$P/zyhome" ] && chmod +x "$P/zyhome"
[ -f "$P/zyperos-update" ] && chmod +x "$P/zyperos-update"
[ -f "$P/p2d" ] && chmod +x "$P/p2d"

# PortMaster's control.txt for the Ports shelf, which launch.sh puts in place
# before each port, and the installer the shelf runs when PortMaster is not on
# the card (src/pminstall.h), with its entry's covers and its page's icon
# (the system art's, res/system-art/original/utility). The devices that
# run ports (the Pixel 2: HAS_PORTS in its device.sh).
if has_helper ports; then
	mkdir -p "$P/portmaster"
	cp "$ROOT/res/portmaster/control.txt" "$ROOT/res/portmaster/libgl_ZyperOS.txt" \
	   "$ROOT/res/portmaster/mod_ZyperOS.txt" "$ROOT/res/portmaster/pm-install.py" \
	   "$ROOT/res/portmaster/cover-light.png" "$ROOT/res/portmaster/cover-dark.png" \
	   "$ROOT/res/portmaster/icon.png" "$P/portmaster/"
	# OpenBOR, the OpenBOR shelf's program (mk/build-openbor.sh), beside the
	# script its "run:" names (mk/card-scripts.sh put it there). The Pixel 2
	# only, as its ports are: only its device.sh has launch.sh run a shelf's
	# script.
	[ -f "$VENDOR_DIR/openbor/OpenBOR" ] ||
		{ echo "payload: no OpenBOR in vendor/; run mk/build-openbor.sh" >&2; exit 1; }
	mkdir -p "$P/openbor"
	cp "$VENDOR_DIR/openbor/OpenBOR" "$VENDOR_DIR/openbor/LICENSE" \
	   "$VENDOR_DIR/openbor/LICENSE.libvpx" "$P/openbor/"
	chmod +x "$P/openbor/OpenBOR"
	# Splore's entry art (tools/genpico8-art.py), beside your PICO-8's script
	# (mk/card-scripts.sh). Never PICO-8 itself: the player brings it into
	# Bios/PICO-8 (src/pico8.h)
	mkdir -p "$P/pico8/bin"
	cp "$ROOT/res/pico8/cover-light.png" \
	   "$ROOT/res/pico8/cover-dark.png" "$ROOT/res/pico8/icon.png" "$P/pico8/"
	# and the library it loads before SDL, which draws PICO-8's screen
	# through the shelf's Shader with its Border (src/p8fx.h)
	if has_helper p8fx; then cp "$P8FX_SO" "$P/pico8/p8fx.so"; fi
fi

# One ROM folder per system. Read from systems.cfg (awk, not sed: folder names
# contain spaces). No .media in them: that is where a system's covers go, and
# whatever puts the first cover there makes it. Made here, it was an empty
# folder in all 41 systems (45 since the PSP, Doom, ColecoVision and TIC-80)
# on every new card, and every update brought them
# back through the pack's .card list (2026-11-06: ".media folders
# basically everywhere").
awk -F'|' '$1=="sys"{gsub(/^[ \t]+|[ \t]+$/,"",$3); print $3}' "$ROOT/config/systems.cfg" |
while IFS= read -r folder; do
	mkdir -p "$OUT/Roms/$folder"
done
# Bios/ is created above and stays FLAT. It is handed to the core as its system
# directory and a core asks for a filename inside it - mgba wants gba_bios.bin,
# mednafen wants syscard3.pce - so a folder per system is a place a BIOS goes to
# be ignored. This line used to read `mkdir -p "$OUT/Bios/GBA"`, directly under a
# comment saying not to litter the card with empty Bios folders, and it shipped
# in v1.0: an empty GBA/ that contradicted the README two directories away.
# Nothing errors when a BIOS lands in it. mgba falls back to its built-in one and
# the only symptom is the boot animation you were trying to enable not appearing.

# The folder's index for System Update (docs/updates.md): every file by
# SHA-256 and size, so the first update downloads only what changed. Last, so
# it lists the folder as it ships. Only ever a hint: the update checks every
# file it keeps against the signed list.
if command -v sha256sum > /dev/null 2>&1; then SHA="sha256sum"; else SHA="shasum -a 256"; fi
( cd "$P" && find . -type f ! -path ./.files | sed 's|^\./||' | LC_ALL=C sort |
  while IFS= read -r f; do
	printf '%s %s %s\n' "$($SHA "$f" | cut -c1-64)" "$(wc -c < "$f" | tr -d ' ')" "$f"
  done ) > "$P/.files"

du -sh "$OUT"
echo "payload ready: $OUT"
