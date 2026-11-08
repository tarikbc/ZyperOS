#!/bin/sh
# Build the J2ME core, FreeJ2ME-Plus: freej2me_plus_libretro.so, and the two
# things it reads from Bios/, its Java app (freej2me_plus-lr.jar) and the Java
# runtime it starts that app with (freej2me_plus_runtime_linux-arm64.tar.gz).
#
# FreeJ2ME-Plus's core is a small C program that runs the emulator, which is
# Java, as a second process and trades frames and input with it over pipes.
# plastron has no Java, so the core's own answer is used: a runtime made with
# jlink (java.base and java.desktop, the two modules the app uses) from the
# system directory, which the core unpacks into Bios/freej2me_plus_runtime/
# the first time a game starts (src/libretro/bundled_jre.c upstream).
#
# TASEmulators/freej2me-plus at bdd5edc, the development branch on
# 2026-11-06 with its CI passing, GPL-3.0 (ObjectWeb ASM, which it bundles,
# is BSD-3-Clause). The core is built in mk/flycast2021.Dockerfile, bullseye's
# gcc 10 on arm64, as the other cores are, and checked as build-small-cores.sh
# checks its cores. The jar and the runtime are built as upstream's CI builds
# them (.github/workflows/libretro.yml): Ant's build-runtime on Temurin 21, the
# classes for Java 8, here Adoptium's aarch64 JDK and Apache Ant, both by
# sha256, on the same image.
#
# Four changes of ZyperOS's own. The core gets mk/patches/freej2me-plus-
# rumble.patch: it stopped a rumble every frame through a function a
# frontend without a motor never gave it, and crashed (the Brick has none).
# It gets mk/patches/freej2me-plus-busybox-tar.patch: it unpacked its
# runtime with "tar -xf", which BusyBox's tar, plastron's, refuses for a
# .tar.gz. It gets mk/patches/freej2me-plus-close-pipes.patch: Diatom puts
# it down with retro_deinit after every game, which ends its Java app but
# kept two pipes to the app open. And the runtime gets a font: Java draws a game's text with its
# own font system, which on Linux asks fontconfig for its fonts, and
# plastron has no fontconfig; the app died at its first text ("Fontconfig
# head is null").
# Without fontconfig Java reads lib/fontconfig.properties instead, so the
# runtime carries one, naming DejaVu Sans (2.37, by sha256, its license
# beside it in lib/fonts) for every Java font.
#
# The core is checked against its sha256. The jar and the archive carry the
# time they were made in every entry, so they are checked by what they hold:
# the sha256 of `shasum -a 256` over every file, unpacked, in byte order of
# their paths.
#
#   vendor/cores/freej2me_plus_libretro.so
#   vendor/freej2me/freej2me_plus-lr.jar
#   vendor/freej2me/freej2me_plus_runtime_linux-arm64.tar.gz
#                    mk/payload.sh ships these two and launch.sh puts them in
#                    Bios/, where the core looks
#
# Usage: mk/build-freej2me.sh [work dir]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${1:-/tmp/zyperos-j2me}

PIN=bdd5edc75b3a83f92496b9aa0bf8ceded2538930      # TASEmulators/freej2me-plus, devel
TAR_SHA=6be5365d26e93d91514627fb28370e478765de131362eeb282429370dea9ef08
JDK=OpenJDK21U-jdk_aarch64_linux_hotspot_21.0.13_9.tar.gz
JDK_URL=https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.13%2B9/$JDK
JDK_SHA=e3522e707fc7dbe2a72c30746b1e7068301a0213ecd213d4491f35a14eb55578
ANT=apache-ant-1.10.15-bin.tar.gz
ANT_URL=https://archive.apache.org/dist/ant/binaries/$ANT
ANT_SHA=71334d7e5d98cfe53d6c429a648a5021137a967378667306c5f613dff5180506   # its sha512 is Apache's published one
DEJAVU=dejavu-fonts-ttf-2.37.tar.bz2
DEJAVU_URL=https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/$DEJAVU
DEJAVU_SHA=fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7
SHA=246d9a5bfe5444ea781b2d640970202d2d40375a35c0cd74fdead8d8db9f1fba
JAR_SHA=9924b16d5e525fa7d30876916c3ad3b4b0acc5af49fc8cf8e574151383662e96
RT_SHA=73dc97a543d942b4ad40f569e41d82e2bbb85011c459ae9f51a46fab172b4e23

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
command -v curl   >/dev/null || { echo "need curl" >&2; exit 1; }
docker build --platform linux/arm64 -q -f "$ROOT/mk/flycast2021.Dockerfile" -t zyperos-flycast2021 "$ROOT/mk" >/dev/null
mkdir -p "$WORK"

# get FILE URL SHA256: a download, checked
get() {
	[ -f "$WORK/$1" ] || curl -sSfL -o "$WORK/$1" "$2"
	got=$(shasum -a 256 "$WORK/$1" | cut -d' ' -f1)
	[ "$got" = "$3" ] || { echo "!! $1 sha256 $got, expected $3" >&2; exit 1; }
}
get "freej2me-plus-$PIN.tar.gz" "https://github.com/TASEmulators/freej2me-plus/archive/$PIN.tar.gz" "$TAR_SHA"
get "$JDK" "$JDK_URL" "$JDK_SHA"
get "$ANT" "$ANT_URL" "$ANT_SHA"
get "$DEJAVU" "$DEJAVU_URL" "$DEJAVU_SHA"

SRC="$WORK/freej2me-plus-$PIN"
rm -rf "$SRC" "$WORK/fonts"
tar -xzf "$WORK/freej2me-plus-$PIN.tar.gz" -C "$WORK"
( cd "$SRC" && git apply "$ROOT/mk/patches/freej2me-plus-rumble.patch" \
	&& git apply "$ROOT/mk/patches/freej2me-plus-busybox-tar.patch" \
	&& git apply "$ROOT/mk/patches/freej2me-plus-close-pipes.patch" )
# The font and its license, and the file Java reads for fonts when it finds
# no fontconfig: every logical font is DejaVu Sans, from the runtime's own
# lib/fonts ($JRE_LIB_FONTS)
mkdir -p "$WORK/fonts"
tar -xjf "$WORK/$DEJAVU" -C "$WORK/fonts" --strip-components=2 dejavu-fonts-ttf-2.37/ttf/DejaVuSans.ttf
tar -xjf "$WORK/$DEJAVU" -C "$WORK/fonts" --strip-components=1 dejavu-fonts-ttf-2.37/LICENSE
printf '%s\n' \
	'# FreeJ2ME-Plus on plastron, which has no fontconfig (mk/build-freej2me.sh)' \
	'version=1' \
	'allfonts.latin-1=DejaVu Sans' \
	'sequence.allfonts=latin-1' \
	'filename.DejaVu_Sans=$JRE_LIB_FONTS/DejaVuSans.ttf' > "$WORK/fonts/fontconfig.properties"

docker run --rm --platform linux/arm64 -v "$WORK:/work" -w "/work/freej2me-plus-$PIN" \
	-e JDK="$JDK" -e ANT="$ANT" zyperos-flycast2021 bash -c '
set -e
make -C src/libretro -j6 platform=unix > /work/core.log 2>&1 || { tail -30 /work/core.log >&2; exit 1; }
so=src/libretro/freej2me_plus_libretro.so
strip --strip-unneeded $so
[ "$(nm -D -u $so | grep -v " w " | grep -vc "@")" = 0 ] \
  || { echo "!! undefined symbols in $so - it will not dlopen" >&2; exit 1; }
mkdir -p /tmp/jdk /tmp/ant
tar -xzf /work/$JDK -C /tmp/jdk --strip-components=1
tar -xzf /work/$ANT -C /tmp/ant --strip-components=1
export JAVA_HOME=/tmp/jdk PATH=/tmp/jdk/bin:/tmp/ant/bin:$PATH
ant -noinput build-runtime -Dsource.version=8 -Dtarget.version=8 \
	-Dplatform=linux-arm64 -Druntime.jdk.home=/tmp/jdk > /work/ant.log 2>&1 \
  || { tail -30 /work/ant.log >&2; exit 1; }
# The font into the runtime: unpacked, added and packed again, in GNU tar
# and in name order, as the core unpacks it with the device'"'"'s tar
rt=build/freej2me_plus_runtime_linux-arm64.tar.gz
rm -rf /tmp/rt && mkdir /tmp/rt && tar -xzf $rt -C /tmp/rt
R=/tmp/rt/freej2me_plus_runtime/linux-arm64
mkdir -p $R/lib/fonts
cp /work/fonts/DejaVuSans.ttf $R/lib/fonts/
cp /work/fonts/LICENSE $R/lib/fonts/LICENSE-DejaVu
cp /work/fonts/fontconfig.properties $R/lib/
tar -C /tmp/rt --sort=name --owner=0 --group=0 --numeric-owner -cf - freej2me_plus_runtime | gzip -n > $rt
'

# tree_sha DIR: the sha256 of every file under DIR, in byte order of paths
tree_sha() {
	( cd "$1" && find . -type f | LC_ALL=C sort |
	  while IFS= read -r f; do shasum -a 256 "$f"; done | shasum -a 256 | cut -d' ' -f1 )
}
SO="$SRC/src/libretro/freej2me_plus_libretro.so"
JAR="$SRC/build/freej2me_plus-lr.jar"
RT="$SRC/build/freej2me_plus_runtime_linux-arm64.tar.gz"
rm -rf "$WORK/check" && mkdir -p "$WORK/check/jar" "$WORK/check/rt"
( cd "$WORK/check/jar" && unzip -q "$JAR" )
tar -xzf "$RT" -C "$WORK/check/rt"
# The runtime target copies the jar into the runtime's bin/ as well, with its
# own entry times; the core runs the one in Bios/, checked above, so the copy
# is left out of the runtime's sum, which is jlink's output and repeats
rm "$WORK/check/rt/freej2me_plus_runtime/linux-arm64/bin/freej2me_plus-lr.jar"
GOT=$(shasum -a 256 "$SO" | cut -d' ' -f1)
GOT_JAR=$(tree_sha "$WORK/check/jar")
GOT_RT=$(tree_sha "$WORK/check/rt")
if [ "$GOT" != "$SHA" ] || [ "$GOT_JAR" != "$JAR_SHA" ] || [ "$GOT_RT" != "$RT_SHA" ]; then
	echo "!! freej2me_plus sha256 $GOT, expected $SHA" >&2
	echo "!! jar contents sha256 $GOT_JAR, expected $JAR_SHA" >&2
	echo "!! runtime contents sha256 $GOT_RT, expected $RT_SHA" >&2
	echo "  The toolchain, the JDK or the source changed. Verify the core, then" >&2
	echo "  update SHA, JAR_SHA and RT_SHA here." >&2
	exit 1
fi
# No symbolic link in the runtime: Bios/ is exFAT, which cannot hold one,
# and the core's tar would stop at the first
[ -z "$(find "$WORK/check/rt" -type l)" ] || { echo "!! the runtime holds links" >&2; exit 1; }
mkdir -p "$ROOT/vendor/cores" "$ROOT/vendor/freej2me"
cp "$SO" "$ROOT/vendor/cores/freej2me_plus_libretro.so"
cp "$JAR" "$RT" "$ROOT/vendor/freej2me/"
# What launch.sh compares to copy the two to Bios/ again: the commit and
# what the jar and the runtime hold, which ZyperOS's font changes too
printf '%s %s %s\n' "$PIN" "$JAR_SHA" "$RT_SHA" > "$ROOT/vendor/freej2me/.pin"
echo "  built  freej2me_plus ($PIN)"
echo "  sha256 $GOT"
# Which patches are in the core, for mk/payload.sh to hold it to
# (mk/core-stamp.sh)
sh "$ROOT/mk/core-stamp.sh" write freej2me_plus
