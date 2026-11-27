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
# FLAVOR=r36s is ZyperOS on the R36S, a test card (ZyperOS's
# docs/devices/r36s.md): the Pixel 2's system with the R36S's kernel,
# device tree and launcher.
FLAVOR=${FLAVOR:-zyperos}
DEFCONFIG=px2_defconfig
case $FLAVOR in
zyperos) OUTPUT=/work/output ;;
base) OUTPUT=/work/output-base ;;
brick) OUTPUT=/work/output-brick DEFCONFIG=brick_defconfig ;;
smartpro) OUTPUT=/work/output-smartpro DEFCONFIG=smartpro_defconfig ;;
qemu) OUTPUT=/work/output-qemu DEFCONFIG=qemu_defconfig ;;
r36s) OUTPUT=/work/output-r36s DEFCONFIG=r36s_defconfig ;;
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

# The development setting (make DEV=1, make brick DEV=1): this output's
# builds compile through ccache and build again whatever input changed
# (plastron_fresh, below). Off unless asked for, so a build without it is
# the build it always was. An output keeps it once set, because its compiler
# wrapper then goes through ccache; it leaves it only clean (make DEV=0
# CLEAN=1). Never for a release, which starts from an empty output.
dev_mark=$OUTPUT/.plastron-dev
if [ "${PLASTRON_DEV:-}" = 0 ] && [ -e "$dev_mark" ] && [ -z "${PLASTRON_CLEAN:-}" ]; then
	echo "plastron: $OUTPUT is a development output (its compiler wrapper goes through ccache):" >&2
	echo "plastron: it leaves that only clean, make DEV=0 CLEAN=1 (make brick DEV=0 CLEAN=1, ...), about an hour" >&2
	exit 1
fi

# CLEAN=1 (make CLEAN=1, make brick CLEAN=1): the kept output emptied first, for what a
# kept one cannot follow (plastron_fresh says when)
if [ -n "${PLASTRON_CLEAN:-}" ] && [ -z "${PLASTRON_RELEASE:-}" ] && [ -d "$OUTPUT" ]; then
	echo "plastron: emptying $OUTPUT for a clean build"
	find "$OUTPUT" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
fi

DEV=
if [ -z "${PLASTRON_RELEASE:-}" ]; then
	if [ "${PLASTRON_DEV:-}" = 1 ] || { [ "${PLASTRON_DEV:-}" != 0 ] && [ -e "$dev_mark" ]; }; then
		DEV=1
		echo "plastron: a development build of $OUTPUT: ccache, and every input it does not watch checked (DEV=0 CLEAN=1 leaves it)"
	fi
fi

make -C "$BUILDROOT" O="$OUTPUT" BR2_EXTERNAL=/src/buildroot BR2_DL_DIR=/work/dl $DEFCONFIG
if [ "$FLAVOR" = zyperos ]; then
	"$BUILDROOT/support/kconfig/merge_config.sh" -m -O "$OUTPUT" \
		"$OUTPUT/.config" /src/buildroot/board/zyperos/zyperos.config >/dev/null
	make -C "$OUTPUT" olddefconfig >/dev/null
elif [ "$FLAVOR" = r36s ]; then
	"$BUILDROOT/support/kconfig/merge_config.sh" -m -O "$OUTPUT" \
		"$OUTPUT/.config" /src/buildroot/board/zyperos/zyperos.config \
		/src/buildroot/board/r36s/r36s.config >/dev/null
	make -C "$OUTPUT" olddefconfig >/dev/null
elif [ "$FLAVOR" = qemu ]; then
	"$BUILDROOT/support/kconfig/merge_config.sh" -m -O "$OUTPUT" \
		"$OUTPUT/.config" /src/buildroot/board/zyperos/zyperos.config \
		/src/buildroot/board/qemu/zyperos-qemu.config >/dev/null
	make -C "$OUTPUT" olddefconfig >/dev/null
fi
# A development build compiles through ccache, its cache shared by every
# output in /work/ccache (board/zyperos/dev.config): a package built again
# for a changed input takes its unchanged objects from it
if [ -n "$DEV" ]; then
	"$BUILDROOT/support/kconfig/merge_config.sh" -m -O "$OUTPUT" \
		"$OUTPUT/.config" /src/buildroot/board/zyperos/dev.config >/dev/null
	make -C "$OUTPUT" olddefconfig >/dev/null
	touch "$dev_mark"
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
		vars="$vars ${var}_SITE ${var}_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS ${var}_PLASTRON_INPUTS"
	done
	[ -n "$pkgs" ] || return 0
	# RSYNC_VCS_EXCLUSIONS: Buildroot's own copy leaves .git and the like
	# behind, so the fingerprint does too, or every fetch would look like a
	# change to the launcher's source
	eval "$(make -C "$out" -s --no-print-directory printvars VARS="RSYNC_VCS_EXCLUSIONS $vars" QUOTED_VARS=YES)"
	empty=$(mktemp -d)
	for pkg in $pkgs; do
		var=$(printf '%s' "$pkg" | tr 'a-z-' 'A-Z_')
		eval "site=\${${var}_SITE:-} excl=\${${var}_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS:-} inputs=\${${var}_PLASTRON_INPUTS:-}"
		if [ ! -d "$site" ]; then
			echo "plastron: $pkg's source $site is not here; its build stays as it is" >&2
			continue
		fi
		if [ -n "$DEV" ]; then
			# A development build goes by contents: every session builds from
			# its own worktree, whose files have their own times, and the same
			# source need not be built twice. (Not the same record as the other
			# way, so a build that changes ways builds these again once.)
			# shellcheck disable=SC2086 # the exclusions are rsync arguments
			now=$(rsync -a --dry-run --out-format='%n' $RSYNC_VCS_EXCLUSIONS $excl "$site/" "$empty/" |
				grep -v '/$' | LC_ALL=C sort | tr '\n' '\0' | (cd "$site" && xargs -0r sha256sum) |
				sha256sum | cut -d' ' -f1)
		else
			# shellcheck disable=SC2086 # the exclusions are rsync arguments
			now=$(rsync -a --dry-run --out-format='%n %l %M' $RSYNC_VCS_EXCLUSIONS $excl "$site/" "$empty/" |
				LC_ALL=C sort | sha256sum | cut -d' ' -f1)
		fi
		# Files outside its folder that it builds from (<PKG>_PLASTRON_INPUTS
		# in its .mk: zyperos-eq compiles ZyperOS's src/eqdsp.h), by
		# contents either way; one that is missing counts as a change
		if [ -n "$inputs" ]; then
			# shellcheck disable=SC2086 # a list of paths
			xin=$(for f in $inputs; do sha256sum "$f" 2>/dev/null || echo "missing $f"; done | sha256sum | cut -d' ' -f1)
			now=$(printf '%s %s\n' "$now" "$xin" | sha256sum | cut -d' ' -f1)
		fi
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

# A kept output is only as good as Buildroot's idea of what changed, and
# Buildroot rebuilds a package for a new version and nothing else. So before
# each development build (DEV=1) every input it does not watch is
# fingerprinted by its contents, and what changed is rebuilt, said, and
# recorded:
#
#   - a local package's source (SITE_METHOD = local: the launcher, Diatom,
#     the gauge, the splash, ...), copied once and never looked at again.
#     Found 2026-11-06: the gauge that follows Settings > Side Lights was
#     merged on 2026-11-05, and every image built here after that held the
#     gauge from before it;
#   - every package of this tree's own (buildroot/package/<name>/: its .mk,
#     patches and files), and every package's patches in BR2_GLOBAL_PATCH_DIR:
#     a patch added to a version that did not change is never applied;
#   - the kernel's patches, config and device trees, and busybox's config
#     fragments: a new kernel patch was not applied on 2026-11-05, and the
#     rumble motor of that test kernel was latched on from boot;
#   - the .config itself (the defconfig and the fragments merged into it).
#     A change to a package's option rebuilds that package; one to the
#     toolchain or the architecture cannot be built over, and stops the build
#     (CLEAN=1: make CLEAN=1, make brick CLEAN=1, empties the output first).
#
# Contents, not times: every session builds from its own worktree, whose
# files have their own times, and the same source must not be built twice.
# Each changed package is dircleaned, all in one make, so it is built again
# from what its source is now.
plastron_fp() {
	# sha256 of every file under the paths, with its path below them
	for p in "$@"; do
		[ -e "$p" ] || continue
		( cd "$p" 2>/dev/null && find . -type f -print0 | LC_ALL=C sort -z | xargs -0r sha256sum ) ||
			sha256sum "$p"
	done | sha256sum | cut -d' ' -f1
}

plastron_fresh() {
	out=$1 state=$1/.plastron-src why=
	mkdir -p "$state"
	# What a build records, for the next to compare with: none on a first
	# build, which has nothing to be stale
	first=; [ -f "$state/config" ] || first=1

	# The .config, its comments and empty lines aside
	grep -v '^#\|^$' "$out/.config" | LC_ALL=C sort > "$state/config.new"
	if [ -z "$first" ] && ! cmp -s "$state/config" "$state/config.new"; then
		changed=$(LC_ALL=C comm -3 "$state/config" "$state/config.new" | sed 's/^[[:space:]]*//; s/=.*//' | LC_ALL=C sort -u)
		echo "plastron: the .config changed since the last build of $out:"
		printf '%s\n' "$changed" | sed 's/^/plastron:   /'
		# ccache switched on or off: the compiler's wrapper is host-gcc-final's,
		# built again; what was built without it stays as it is, the same code
		if printf '%s\n' "$changed" | grep -q '^BR2_CCACHE'; then
			echo "plastron: ccache changed: host-gcc-final is built again for its compiler wrapper (about 10 min)"
			why="$why host-gcc-final"
		fi
		if printf '%s\n' "$changed" | grep -q '^BR2_\(TOOLCHAIN\|GCC\|BINUTILS\|KERNEL_HEADERS\|ARCH\|aarch64\|arm\|cortex\|GLOBAL_PATCH_DIR\|SHARED_\|STATIC_\|OPTIMIZE\|ENABLE_\|REPRODUCIBLE\|TARGET_OPTIMIZATION\|PER_PACKAGE\)'; then
			echo "plastron: that changes how everything is built, which a kept output cannot follow:" >&2
			echo "plastron: build it clean (make CLEAN=1, make brick CLEAN=1, ...), about an hour" >&2
			rm -f "$state/config.new"
			exit 1
		fi
		# A package's own options: BR2_PACKAGE_<NAME>[_...], the longest name
		# that is a package
		for sym in $changed; do
			case $sym in BR2_PACKAGE_*) ;; BR2_LINUX_KERNEL*) why="$why linux" ;; *) continue ;; esac
			rest=${sym#BR2_PACKAGE_}
			while [ -n "$rest" ]; do
				pkg=$(printf '%s' "$rest" | tr 'A-Z_' 'a-z-')
				if [ -d "/src/buildroot/package/$pkg" ] || [ -d "$BUILDROOT/package/$pkg" ]; then
					why="$why $pkg"; break
				fi
				case $rest in *_*) rest=${rest%_*} ;; *) rest= ;; esac
			done
		done
	fi

	# Each enabled package's inputs Buildroot does not watch
	eval "$(make -C "$out" -s --no-print-directory printvars QUOTED_VARS=YES \
		VARS="BR2_GLOBAL_PATCH_DIR BR2_LINUX_KERNEL_PATCH BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE \
		BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES BR2_LINUX_KERNEL_CUSTOM_DTS_PATH \
		BR2_PACKAGE_BUSYBOX_CONFIG BR2_PACKAGE_BUSYBOX_CONFIG_FRAGMENT_FILES" 2>/dev/null)"
	# Buildroot's string options keep their quotes
	for v in BR2_GLOBAL_PATCH_DIR BR2_LINUX_KERNEL_PATCH BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE \
		BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES BR2_LINUX_KERNEL_CUSTOM_DTS_PATH \
		BR2_PACKAGE_BUSYBOX_CONFIG BR2_PACKAGE_BUSYBOX_CONFIG_FRAGMENT_FILES; do
		eval "$v=\$(printf '%s' \"\${$v:-}\" | tr -d '\"')"
	done
	pkgs=$( { ls /src/buildroot/package; for d in ${BR2_GLOBAL_PATCH_DIR:-}; do ls "$d" 2>/dev/null; done; } | LC_ALL=C sort -u)
	for pkg in $pkgs linux; do
		var=$(printf '%s' "$pkg" | tr 'a-z-' 'A-Z_')
		case $pkg in
		linux) grep -q '^BR2_LINUX_KERNEL=y' "$out/.config" || continue
			now=$(plastron_fp ${BR2_LINUX_KERNEL_PATCH:-} ${BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE:-} \
				${BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES:-} ${BR2_LINUX_KERNEL_CUSTOM_DTS_PATH:-}) ;;
		*) grep -q "^BR2_PACKAGE_$var=y" "$out/.config" || continue
			extra=
			[ "$pkg" = busybox ] && extra="${BR2_PACKAGE_BUSYBOX_CONFIG:-} ${BR2_PACKAGE_BUSYBOX_CONFIG_FRAGMENT_FILES:-}"
			# shellcheck disable=SC2086 # paths, each a word
			now=$(plastron_fp "/src/buildroot/package/$pkg" \
				$(for d in ${BR2_GLOBAL_PATCH_DIR:-}; do echo "$d/$pkg"; done) $extra) ;;
		esac
		was=$(cat "$state/inputs-$pkg" 2>/dev/null || true)
		echo "$now" > "$state/inputs-$pkg.new"
		[ -n "$first" ] || [ "$now" = "$was" ] || why="$why $pkg"
	done

	[ -z "$first" ] || echo "plastron: $out has no record of its inputs yet: this build records them, and compares from the next (CLEAN=1 if you doubt it)"
	# An output built before ccache was on: its compiler wrapper has none
	if [ -n "$first" ] && grep -q '^BR2_CCACHE=y' "$out/.config" && [ ! -x "$out/host/bin/ccache" ] &&
		ls "$out"/build/host-gcc-final-*/.stamp_host_installed > /dev/null 2>&1; then
		echo "plastron: ccache is new in $out: host-gcc-final is built again for its compiler wrapper (about 10 min)"
		why="$why host-gcc-final"
	fi
	why=$(printf '%s\n' $why | LC_ALL=C sort -u | tr '\n' ' ')
	if [ -n "${why% }" ]; then
		echo "plastron: built again, their inputs changed: $why"
		# shellcheck disable=SC2046 # one target a package
		make -C "$out" -s --no-print-directory $(for p in $why; do echo "$p-dirclean"; done)
	fi
}

# After the build: the records the next build compares with, and what this
# one built and what it took from the output as it was, said and kept in
# <output>/plastron-reused.txt
plastron_record() {
	out=$1 state=$1/.plastron-src started=$2
	mv -f "$state/config.new" "$state/config" 2>/dev/null || true
	for f in "$state"/inputs-*.new; do [ -e "$f" ] && mv -f "$f" "${f%.new}"; done
	built= kept=0
	for stamp in "$out"/build/*/.stamp_built; do
		[ -e "$stamp" ] || continue
		d=${stamp%/.stamp_built}; d=${d##*/}
		if [ "$stamp" -nt "$started" ]; then built="$built $d"; else kept=$((kept + 1)); fi
	done
	{
		echo "built $(date -u '+%Y-%m-%d %H:%M:%S') UTC from ${PLASTRON_COMMIT:-?}"
		echo "built again:${built:- nothing}"
		echo "taken as they were: $kept packages"
		( cd "$out/build" && for stamp in */.stamp_built; do
			[ "$stamp" -nt "$started" ] || echo "  ${stamp%/.stamp_built}"
		done )
	} > "$out/plastron-reused.txt"
	echo "plastron: built again:${built:- nothing}"
	echo "plastron: $kept packages taken from $out as they were (the list: $out/plastron-reused.txt)"
	echo "plastron: an incremental build never empties target/: a file removed from an overlay or a package stays in the image until a clean build"
	rm -f "$started"
}

# A development build's checks and record; any other builds as it always did
if [ -n "$DEV" ]; then
	plastron_fresh "$OUTPUT"
	plastron_started=$OUTPUT/.plastron-src/started
	touch "$plastron_started"
fi

make -C "$OUTPUT" "$@"
[ -z "$DEV" ] || plastron_record "$OUTPUT" "$plastron_started"
# Which plastron this output was last built from, for make sdk to name in the
# SDK it packs from it (the Makefile passes it; the container has no git)
[ -z "${PLASTRON_COMMIT:-}" ] || echo "$PLASTRON_COMMIT" > "$OUTPUT/plastron-commit"
