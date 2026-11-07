#!/bin/sh
# Runs inside the build container. /src is this repository (read-only);
# /work is a Docker volume, because Buildroot needs a case-sensitive
# filesystem and the Mac's isn't.
set -eu

BUILDROOT_VERSION=2026.02.3
BUILDROOT=/work/buildroot-$BUILDROOT_VERSION
# FLAVOR=zyperos (the default) is the base system with ZyperOS on top;
# FLAVOR=base is the base system alone. Separate outputs, so switching
# never leaves one's files in the other. FLAVOR=brick is the TrimUI Brick's
# system, its own defconfig with its own toolchain. FLAVOR=qemu is ZyperOS on
# QEMU's virt machine, standing in for the Pixel 2 (docs/qemu.md).
FLAVOR=${FLAVOR:-zyperos}
DEFCONFIG=px2_defconfig
case $FLAVOR in
zyperos) OUTPUT=/work/output ;;
base) OUTPUT=/work/output-base ;;
brick) OUTPUT=/work/output-brick DEFCONFIG=brick_defconfig ;;
qemu) OUTPUT=/work/output-qemu DEFCONFIG=qemu_defconfig ;;
*) echo "unknown FLAVOR $FLAVOR" >&2; exit 1 ;;
esac
# A release builds in an empty output folder of its own (scripts/release.sh)
OUTPUT=${PLASTRON_OUTPUT:-$OUTPUT}

# One build of an output at a time (scripts/lock.sh); builds of different
# outputs go on side by side
. /src/scripts/lock.sh
plastron_lock "$OUTPUT"

mkdir -p /work/dl
if [ ! -d "$BUILDROOT" ]; then
	tarball=/work/dl/buildroot-$BUILDROOT_VERSION.tar.xz
	[ -f "$tarball" ] || wget -q -O "$tarball" "https://buildroot.org/downloads/buildroot-$BUILDROOT_VERSION.tar.xz"
	tar -C /work -xf "$tarball"
fi

make -C "$BUILDROOT" O="$OUTPUT" BR2_EXTERNAL=/src/buildroot BR2_DL_DIR=/work/dl $DEFCONFIG
if [ "$FLAVOR" = zyperos ]; then
	"$BUILDROOT/support/kconfig/merge_config.sh" -m -O "$OUTPUT" \
		"$OUTPUT/.config" /src/buildroot/board/zyperos/zyperos.config >/dev/null
	make -C "$OUTPUT" olddefconfig >/dev/null
elif [ "$FLAVOR" = qemu ]; then
	"$BUILDROOT/support/kconfig/merge_config.sh" -m -O "$OUTPUT" \
		"$OUTPUT/.config" /src/buildroot/board/zyperos/zyperos.config \
		/src/buildroot/board/qemu/zyperos-qemu.config >/dev/null
	make -C "$OUTPUT" olddefconfig >/dev/null
fi
# Buildroot copies a local package's source (SITE_METHOD = local: the
# launcher, Diatom, the gauge, the splash, ...) into the output once and never
# looks at it again, so an image built after a change to that source still
# carries the old binary. Found 2026-11-06: the gauge that follows Settings >
# Side Lights was merged on 2026-11-05, and every image built here after that
# held the gauge from before it. So before each build, every enabled local
# package's source is fingerprinted (each file the build would copy, with its
# size and time, through the package's own rsync exclusions) and a package
# whose fingerprint changed, or was never taken, is dircleaned and so built
# again from what its source is now.
plastron_fresh_locals() {
	out=$1 state=$1/.plastron-src vars= pkgs=
	mkdir -p "$state"
	for mk in /src/buildroot/package/*/*.mk; do
		grep -q '_SITE_METHOD *= *local' "$mk" || continue
		pkg=${mk##*/}; pkg=${pkg%.mk}
		var=$(printf '%s' "$pkg" | tr 'a-z-' 'A-Z_')
		grep -q "^BR2_PACKAGE_$var=y" "$out/.config" || continue
		pkgs="$pkgs $pkg"
		vars="$vars ${var}_SITE ${var}_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS"
	done
	[ -n "$pkgs" ] || return 0
	# RSYNC_VCS_EXCLUSIONS: Buildroot's own copy leaves .git and the like
	# behind, so the fingerprint does too, or every fetch would look like a
	# change to the launcher's source
	eval "$(make -C "$out" -s --no-print-directory printvars VARS="RSYNC_VCS_EXCLUSIONS $vars" QUOTED_VARS=YES)"
	empty=$(mktemp -d)
	for pkg in $pkgs; do
		var=$(printf '%s' "$pkg" | tr 'a-z-' 'A-Z_')
		eval "site=\${${var}_SITE:-} excl=\${${var}_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS:-}"
		if [ ! -d "$site" ]; then
			echo "plastron: $pkg's source $site is not here; its build stays as it is" >&2
			continue
		fi
		# shellcheck disable=SC2086 # the exclusions are rsync arguments
		now=$(rsync -a --dry-run --out-format='%n %l %M' $RSYNC_VCS_EXCLUSIONS $excl "$site/" "$empty/" |
			LC_ALL=C sort | sha256sum | cut -d' ' -f1)
		was=$(cat "$state/$pkg" 2>/dev/null || true)
		[ "$now" = "$was" ] && continue
		if [ -n "$was" ]; then
			echo "plastron: $pkg's source changed since its last build: building it again"
		else
			echo "plastron: $pkg has no record of its source yet: building it again"
		fi
		make -C "$out" -s --no-print-directory "$pkg-dirclean"
		echo "$now" > "$state/$pkg"
	done
	rmdir "$empty"
}
plastron_fresh_locals "$OUTPUT"

make -C "$OUTPUT" "$@"
# Which plastron this output was last built from, for make sdk to name in the
# SDK it packs from it (the Makefile passes it; the container has no git)
[ -z "${PLASTRON_COMMIT:-}" ] || echo "$PLASTRON_COMMIT" > "$OUTPUT/plastron-commit"
