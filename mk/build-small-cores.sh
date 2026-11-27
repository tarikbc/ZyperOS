#!/bin/sh
# Build the small cores libretro's buildbot only has in a form plastron cannot
# load: its aarch64 Linux builds of these two link desktop OpenGL (libGL.so.1),
# and the devices have OpenGL ES alone (Panfrost, Mesa without desktop GL), so
# the buildbot's .so fails to dlopen before a game is tried. Both draw in
# software without it, as ROGUE OS ran them:
#
#   mednafen_vb  Virtual Boy (Beetle VB). Its Makefile's default is no OpenGL;
#                the buildbot turns it on for a 3D mode a single panel has no
#                use for.
#   vecx         Vectrex. HAS_GPU=0 is its software renderer, the vectors drawn
#                into the frame on the CPU.
#
# And three small cores the buildbot has, built here the same way so they are
# pinned to a source and not to whatever the buildbot last built:
#
#   gearcoleco   ColecoVision (Gearcoleco 1.7.6), from platforms/libretro. It
#                reads the console's BIOS from Bios/, as coleco.rom,
#                colecovision.rom or os7.u2. ROGUE OS and ROCKNIX run the
#                ColecoVision on it.
#   gambatte     Game Boy and Game Boy Color (gambatte-libretro), the second
#                core of their shelves beside mGBA (1.0.7, config/systems.cfg).
#                C++, with Debian bullseye's GCC 10: plastron's libstdc++ is
#                newer than the GLIBCXX it asks for (diatom/CORES.md).
#                Patched: gambatte-link.patch, its network Game Link for
#                the Link Cable (src/link.h): a lost peer no longer freezes
#                the game, the bytes cross whole, and the link ends with
#                its game.
#   prboom       Doom (libretro-prboom), the commit ROCKNIX's prboom-lr pins,
#                with prboom.wad, the engine's own data, compiled into the core
#                (src/d_main.c at the pin), so no file of it goes in Bios/.
#                Patched: prboom-turn-speed.patch, the d-pad's turn at the
#                original game's keyboard speed rather than half of it.
#
# A core's patches are mk/patches/<core>-*.patch, applied in name order to the
# unpacked source; a core built with any gets its stamp (mk/core-stamp.sh), so
# mk/payload.sh refuses one built before a patch changed.
#
# Each from a pinned commit of its libretro repository, in the image
# mk/build-flycast2021.sh builds the Dreamcast core in (Debian bullseye's GCC
# 10), checked for undefined symbols as that script checks its core, and then
# against the sha256 here, so a changed toolchain or source cannot slip a
# different binary into vendor/cores.
#
# Usage: mk/build-small-cores.sh [work dir] [core...]
#        mk/build-small-cores.sh --missing [work dir]
# With core names after the work dir, only those are built. --missing builds
# the ones config/systems.cfg names that the vendor folder does not have yet,
# and when it has them all it is done at once, Docker never started:
# tools/release.sh, tools/px2-payload.sh and plastron's card builds run it
# first, so a core added here reaches every machine that makes a card with no
# step to remember (gambatte, in 1.0.7). The cores go into ZYPEROS_VENDOR's
# cores/ when it is set, else vendor/ beside this script.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VENDOR=${ZYPEROS_VENDOR:-$ROOT/vendor}
MISSING=
if [ "${1:-}" = --missing ]; then MISSING=1; shift; fi
WORK=${1:-/tmp/zyperos-small-cores}
[ $# -gt 0 ] && shift
ONLY=" $* "

# name | repository | commit | source sha256 | core sha256 | folder the
# Makefile is in (. for the top) | make arguments
CORES='
mednafen_vb|libretro/beetle-vb-libretro|d5ae54c8e23b573a3b34e888b2d48a588027e92c|ccbed557d3f316c9123f81d1ca5357c5f12100bad6aea3326caefca2a1c08189|959ec69963a6c82dc0b5d4764756e89a161703bcaeeff84ed5c215378ad916c6|.|HAVE_OPENGL=0 HAVE_GLES=0 GIT_VERSION=d5ae54c
vecx|libretro/libretro-vecx|dccff5402a16d1ea7c112095a8e42632bf149506|684808db7e9d3a81647e25e8a7e34df3e0f62ed317df578143055c6536d23a32|cc8dfcab7c79929414f46a2f067729f42d7fbe88ed079c4bf95dfedbedf3b0fa|.|HAS_GPU=0 GIT_VERSION=dccff54 -f Makefile.libretro
gearcoleco|drhelius/Gearcoleco|063fa52151581b356f466905c74a79f9df8d19d7|00c6e98f504141961aec63bb26c3e8f21533b65f5c91eb489e8565d5856a8baa|4028827a2c9160fbbc5589badc3e4a1acd95dbe60b61a9802a410072b23b5699|platforms/libretro|GIT_VERSION=063fa52
gambatte|libretro/gambatte-libretro|77f2567d718b8973d02caa2daa8dce4953faf811|45e63bc79390093b78de025172ea289ded139f4ecf1e8072242f34d7dd540338|2235b57535a3fcf6ebdc64b8eaaf31f29d2e3e477edb98a1f2465182720b8566|.|GIT_VERSION=77f2567
prboom|libretro/libretro-prboom|4849268e7342494193f2bbf2d0128a751cf58da7|36a069cf09053388d719e2694a509cb8d2d152137b66adbef8fccba8373f8879|5acac01568c15e18ce4d590838c20bb86acf5f5bf03f28a394a324b87fe09e74|.|GIT_VERSION=4849268
'

if [ -n "$MISSING" ]; then
	# the libretro cores the shelves name, as mk/payload.sh reads them
	want=" $(awk -F'|' '$1=="sys"{n=split($4, c, ","); for (i = 1; i <= n; i++) {
	             gsub(/^[ \t]+|[ \t]+$/,"",c[i]); print c[i] } }' "$ROOT/config/systems.cfg" | sort -u | tr '\n' ' ')"
	ONLY=" "
	for name in $(echo "$CORES" | cut -d'|' -f1); do
		case $want in *" $name "*) ;; *) continue ;; esac
		[ -f "$VENDOR/cores/${name}_libretro.so" ] || ONLY="$ONLY$name "
	done
	[ "$ONLY" != " " ] || exit 0
	echo "build-small-cores: $VENDOR/cores lacks${ONLY% }; building it now"
fi

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
command -v curl   >/dev/null || { echo "need curl" >&2; exit 1; }
docker build --platform linux/arm64 -q -f "$ROOT/mk/flycast2021.Dockerfile" -t zyperos-flycast2021 "$ROOT/mk" >/dev/null
mkdir -p "$WORK" "$VENDOR/cores"

echo "$CORES" | while IFS='|' read -r name repo pin tar_sha sha dir args; do
	[ -n "$name" ] || continue
	[ "$ONLY" = "  " ] || case $ONLY in *" $name "*) ;; *) continue ;; esac
	base=${repo#*/}
	tar="$WORK/$base-$pin.tar.gz"
	[ -f "$tar" ] || curl -sSfL -o "$tar" "https://github.com/$repo/archive/$pin.tar.gz"
	got=$(shasum -a 256 "$tar" | cut -d' ' -f1)
	[ "$got" = "$tar_sha" ] || { echo "!! $name source sha256 $got, expected $tar_sha" >&2; exit 1; }
	rm -rf "$WORK/$base-$pin"
	tar -xzf "$tar" -C "$WORK"
	patched=
	for p in "$ROOT/mk/patches/$name-"*.patch; do
		[ -f "$p" ] || continue
		patch -s -p1 -d "$WORK/$base-$pin" < "$p"
		patched=1
	done

	docker run --rm --platform linux/arm64 -v "$WORK/$base-$pin:/src" -w "/src/$dir" \
		-e ARGS="$args" -e SO="${name}_libretro.so" zyperos-flycast2021 bash -c '
set -e
make -j3 platform=unix $ARGS >/dev/null
strip --strip-unneeded $SO
[ "$(nm -D -u $SO | grep -v " w " | grep -vc "@")" = 0 ] \
  || { echo "!! undefined symbols in $SO - it will not dlopen" >&2; exit 1; }
! readelf -d $SO | grep -q "libGL\." \
  || { echo "!! $SO still links libGL" >&2; exit 1; }
'
	so="$WORK/$base-$pin/$dir/${name}_libretro.so"
	got=$(shasum -a 256 "$so" | cut -d' ' -f1)
	if [ "$got" != "$sha" ]; then
		echo "!! $name sha256 $got, expected $sha" >&2
		echo "  The toolchain image or the source changed. Verify the core, then" >&2
		echo "  update its sha256 here." >&2
		exit 1
	fi
	cp "$so" "$VENDOR/cores/${name}_libretro.so"
	echo "  built  $name ($pin)"
	# Which patches are in it, for mk/payload.sh to hold it to
	if [ -n "$patched" ]; then sh "$ROOT/mk/core-stamp.sh" write "$name" "$VENDOR"; fi
done
