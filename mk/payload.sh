#!/bin/sh
# Assemble ZyperOS's card payload: under out/sd/ for the TrimUI Brick, or
# out/sd-pixel2/ with DEVICE=pixel2 for the GKD Pixel 2. plastron, the system
# on both, builds it into its card image and copies it onto the card on first
# boot, so its build sets where the binaries are and where the payload goes:
#
#   DEVICE=brick ZYPEROS_ELF=... MUSE_ELF=... DIATOM_ELF=... VENDOR_DIR=... \
#       BTPLAYER_ELF=... OUT=... mk/payload.sh
#
# BTPLAYER_ELF only for a device with Bluetooth: tools/btplayer.c, which
# plastron's /etc/init.d/bluetooth runs from the card.
#
# TrimUI's own firmware is no longer supported, so there is one Brick payload,
# plastron's. NATIVE=1, which used to choose it, is accepted and ignored. And
# no release zip: it was the Brick's install for TrimUI's firmware, and both
# devices now install from plastron's card image. ZIP= is ignored too.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEVICE=${DEVICE:-brick}
case $DEVICE in
brick)  OUT=${OUT:-$ROOT/out/sd};        LAUNCH=$ROOT/sd/brick/launch.sh ;;
pixel2) OUT=${OUT:-$ROOT/out/sd-pixel2}; LAUNCH=$ROOT/sd/pixel2/launch.sh ;;
*) echo "payload: unknown DEVICE $DEVICE (brick or pixel2)" >&2; exit 1 ;;
esac
P=$OUT/ZyperOS
ZYPEROS_ELF=${ZYPEROS_ELF:-$ROOT/build/zyperos.elf}
MUSE_ELF=${MUSE_ELF:-$ROOT/build/muse}
VENDOR_DIR=${VENDOR_DIR:-$ROOT/vendor}

[ -f "$ZYPEROS_ELF" ] || { echo "run make first"; exit 1; }
DIATOM_ELF=${DIATOM_ELF:-$ROOT/diatom/build/$DEVICE/diatom}
[ -f "$DIATOM_ELF" ] || { echo "no diatom at $DIATOM_ELF (set DIATOM_ELF)"; exit 1; }
[ -f "$VENDOR_DIR/cores/fceumm_libretro.so" ] || { echo "run mk/fetch-vendor.sh first"; exit 1; }

rm -rf "$OUT"
mkdir -p "$P/system-icons" "$P/cores" "$P/res/web" \
         "$OUT/Roms" "$OUT/Music" "$OUT/Audiobooks" "$OUT/Bios" "$OUT/Saves"

cp "$ZYPEROS_ELF" "$P/zyperos.elf"
cp "$MUSE_ELF" "$P/muse"                  # the audio player's engine; Muse cannot play without it
[ -n "$BTPLAYER_ELF" ] && cp "$BTPLAYER_ELF" "$P/btplayer"   # a headset's keys and volume
[ -n "${PORTKEYS_ELF:-}" ] && cp "$PORTKEYS_ELF" "$P/portkeys" # the volume keys during a port (Pixel 2)
[ -n "${UPDATE_ELF:-}" ] && cp "$UPDATE_ELF" "$P/zyperos-update"  # System Update (update/, docs/updates.md)
cp "$LAUNCH" "$P/launch.sh"
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
cp "$ROOT/res/system-icons/"*.png "$P/system-icons/" # pixel home atlases and stills
cp "$ROOT/res/fonts/menu.ttf" "$P/"       # the UI face, and the in-game menu's
cp "$ROOT/res/fonts/title.ttf" "$P/"      # its SemiBold, the title role's, found beside it
cp "$ROOT/res/fonts/zh-menu.otf" "$P/"    # 简体中文's faces and 日本語's, beside
cp "$ROOT/res/fonts/zh-title.otf" "$P/"   # them (tools/cjkfont.py), opened only
cp "$ROOT/res/fonts/ja-menu.otf" "$P/"    # when Chinese or Japanese is drawn
cp "$ROOT/res/fonts/ja-title.otf" "$P/"
cp "$ROOT/res/singles.png" "$P/"          # Muse's cover for Singles, the loose songs
cp "$ROOT/res/zyper.png" "$P/"            # Zyper's photo, on About's The name page
# The interface sounds, every voice (src/uisound.h): the launcher reads them
# from res/sounds/<voice>/ beside res/web/, and plays nothing without them.
# The WAVs only, so a .DS_Store left in the folder does not ship.
for v in "$ROOT"/res/sounds/*/; do
	v=$(basename "$v")
	mkdir -p "$P/res/sounds/$v"
	cp "$ROOT/res/sounds/$v/"*.wav "$P/res/sounds/$v/"
done
# Menu music for a card whose Roms/BGM has none (src/bgm.h): ZyperOS's few
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
cp "$ROOT/LICENSES/MIT-TortOS.txt" "$OUT/Licenses/"
# ...and the licenses of the menu music's tracks, which it names
for f in "$ROOT"/res/bgm/LICENSE-*.txt; do
	cp "$f" "$OUT/Licenses/menu-music-${f##*/LICENSE-}"
done
cp "$DIATOM_ELF" "$P/diatom"
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
# cores, comma separated (the Nintendo DS: DraStic's script and DSperate), and
# each libretro one of them must be here.
for core in $(awk -F'|' '$1=="sys"{n=split($4, c, ","); for (i = 1; i <= n; i++) {
                  gsub(/^[ \t]+|[ \t]+$/,"",c[i]); if (c[i] != "port" && c[i] !~ /^run:/) print c[i] } }' \
              "$ROOT/config/systems.cfg" | sort -u); do
	[ -f "$P/cores/${core}_libretro.so" ] || missing="$missing $core"
done
if [ -n "$missing" ]; then
	echo "payload: systems.cfg needs cores that vendor/ does not have:$missing" >&2
	echo "payload: run mk/fetch-vendor.sh, mk/build-mgba-bridge.sh, mk/build-dsperate.sh," >&2
	echo "payload: mk/build-fake08.sh, mk/build-flycast2021.sh, mk/build-scummvm.sh and mk/build-freej2me.sh" >&2
	exit 1
fi

chmod +x "$P/launch.sh" "$P/zyperos.elf" "$P/diatom" "$P/muse"
[ -f "$P/btplayer" ] && chmod +x "$P/btplayer"
[ -f "$P/portkeys" ] && chmod +x "$P/portkeys"
[ -f "$P/zyperos-update" ] && chmod +x "$P/zyperos-update"

# The headsets' ALSA config: the Brick's launch.sh sources it at boot, and
# ZyperOS runs it after a pair or a forget.
[ "$DEVICE" = brick ] && cp "$ROOT/sd/zyperos/bt-alsa.sh" "$P/"

# PortMaster's control.txt for the Ports shelf, which launch.sh puts in place
# before each port. Only the Pixel 2's launch.sh runs ports.
if [ "$DEVICE" = pixel2 ]; then
	mkdir -p "$P/portmaster"
	cp "$ROOT/res/portmaster/control.txt" "$ROOT/res/portmaster/libgl_ZyperOS.txt" \
	   "$ROOT/res/portmaster/mod_ZyperOS.txt" "$P/portmaster/"
	# OpenBOR, the OpenBOR shelf's program (mk/build-openbor.sh), and the
	# script its "run:" names. The Pixel 2 only, as its ports are: only its
	# launch.sh runs a shelf's script.
	[ -f "$VENDOR_DIR/openbor/OpenBOR" ] ||
		{ echo "payload: no OpenBOR in vendor/; run mk/build-openbor.sh" >&2; exit 1; }
	mkdir -p "$P/openbor"
	cp "$VENDOR_DIR/openbor/OpenBOR" "$VENDOR_DIR/openbor/LICENSE" \
	   "$VENDOR_DIR/openbor/LICENSE.libvpx" "$ROOT/sd/pixel2/OpenBOR.sh" "$P/openbor/"
	chmod +x "$P/openbor/OpenBOR" "$P/openbor/OpenBOR.sh"
fi

# One ROM folder per system. Read from systems.cfg (awk, not sed: folder names
# contain spaces). No .media in them: that is where a system's covers go, and
# whatever puts the first cover there makes it. Made here, it was an empty
# folder in all 41 systems on every new card, and every update brought them
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
