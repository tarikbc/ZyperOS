#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# make check-kitbuild: a test kit's code reaches a kit alone (src/kit.h;
# docs/building.md, "Test kits").
#
#   1. mk/cross.mk, for each device: a VERSION without "-kit" compiles no
#      ZYPEROS_KIT_BUILD and links no src/device/<id>_kit.c; a kit's VERSION
#      adds the define and its platform layer's kit file, where it has one, and
#      nothing else to the line or the sources.
#   2. The files with a kit's hooks, compiled on the host without the define
#      and with it: without, no kit symbol or string in their objects and the
#      Brick's profile with its GPU shaders and frames off; with, all of it
#      there, so this cannot pass by finding nothing. The hardware report's
#      check (tools/hwreport-check.c) runs as a kit's too.
#   3. The host launcher, build-native/zyperos (make native; make check
#      builds it), which is never a kit: no kit symbol or string in it.
#   4. No device's start script (sd/*/device.sh) seeds Diatom's present file:
#      a kit's launcher does (src/device/brick_kit.c).
#   5. That file's place (diatom/port/present_file.h), where the kit seeds it
#      and Diatom's Brick port reads it: the handheld's own settings folder
#      (USERDATA_PATH: .userdata/smartpro on a Smart Pro), the Brick's with
#      none; and both sides take it from there.
#
# The device's own program (zyperos.elf, plastron's SDK) is held to a
# release's bytes by hand, in the change that touches this: a build of main
# and of the change without "-kit", compared.
#
#   DEVICE_PROFILES="..." DEVICE_BUILDS="..." sh tools/kitbuild-check.sh
set -u
cd "$(dirname "$0")/.." || exit 1
CC=${CC:-cc}
fails=0
checks=0
ck() { # COND-STATUS WHAT
	checks=$((checks + 1))
	if [ "$1" -ne 0 ]; then
		fails=$((fails + 1))
		echo "  FAIL $2"
	fi
}
T=$(mktemp -d "${TMPDIR:-/tmp}/kitbuild.XXXXXX") || exit 1
trap 'rm -rf "$T"' EXIT
trap 'exit 1' INT TERM

# Strings and symbols only a kit has: its page, its log lines, its report's
# PMIC and lights lines, its file for Diatom, and its two hooks
KIT_STRINGS='Hardware Check|hwcheck:|PMIC registers|LED facts|diatom-present'
KIT_SYMBOLS='kit_page|kit_hw_facts|kit_present_seed'

# ---- 1. the device build's line and sources ---------------------------------
vars() { # DEVICE VERSION: "CFLAGS=..." then "SRC=...", a space between words
	printf 'kitbuild-vars:\n\t@:$(info CFLAGS=$(CFLAGS))$(info SRC=$(SRC))\n' |
		make -s --no-print-directory -f mk/cross.mk -f - DEVICE="$1" VERSION="$2" \
			BUILD="$T/cross" kitbuild-vars | sed 's/  */ /g; s/ $//'
}
for d in $(ls config/device/*.mk | sed 's|.*/||; s|\.mk$||'); do
	vars "$d" 9.9.9 > "$T/vars-rel" 2>&1 || { ck 1 "$d: mk/cross.mk's line for a release: $(head -3 "$T/vars-rel")"; continue; }
	vars "$d" 9.9.9-kit.1 > "$T/vars-kit" 2>&1 || { ck 1 "$d: mk/cross.mk's line for a kit: $(head -3 "$T/vars-kit")"; continue; }
	! grep -q 'ZYPEROS_KIT_BUILD' "$T/vars-rel" && ! grep -q '_kit\.c' "$T/vars-rel"
	ck $? "$d: a release's build has no ZYPEROS_KIT_BUILD and no kit file"
	# The kit file is its platform layer's (ZYDEV_BACKEND): the Smart Pro
	# runs the Brick's
	b=$(sed -n 's/^ZYDEV_BACKEND = //p' "config/device/$d.mk")
	kitfile=
	[ -f "src/device/${b:-$d}_kit.c" ] && kitfile=" src/device/${b:-$d}_kit.c"
	# The kit's line and sources less what a kit adds, its version made the
	# release's: the release's own, word for word
	sed -e 's/9\.9\.9-kit\.1/9.9.9/' -e 's/ -DZYPEROS_KIT_BUILD$//' -e "s|^\(SRC=.*\)${kitfile}\$|\\1|" \
		"$T/vars-kit" > "$T/vars-kit-less"
	cmp -s "$T/vars-rel" "$T/vars-kit-less" && grep -q -- '-DZYPEROS_KIT_BUILD$' "$T/vars-kit" &&
		{ [ -z "$kitfile" ] || grep -q -- "^SRC=.*${kitfile}\$" "$T/vars-kit"; }
	ck $? "$d: a kit adds ZYPEROS_KIT_BUILD${kitfile:+ and$kitfile} to a release's build, and nothing else"
done

# ---- 2. the hooks' files, without the define and with it ---------------------
# The shared files as the device build compiles them (warnings are not
# errors there); the kit's own file with -Werror, as it is new
F="-std=gnu11 -D_GNU_SOURCE -O1 -Wall -Wextra -Wno-unused-parameter -Isrc"
F="$F -DZYPEROS_VERSION=\"check\" -DZYPEROS_PROFILE=device_profile_brick"
if SDL=$(pkg-config --cflags sdl2 SDL2_image SDL2_ttf 2> /dev/null); then
	for mode in rel kit; do
		K=
		[ "$mode" = kit ] && K=-DZYPEROS_KIT_BUILD
		mkdir -p "$T/$mode"
		for src in src/about_screen.c src/hwreport.c src/xfer_screen.c src/device/brick_hw.c src/device/brick_profile.c; do
			# shellcheck disable=SC2086
			$CC $F $K $SDL -c -o "$T/$mode/$(basename "$src" .c).o" "$src" 2> "$T/err" ||
				{ ck 1 "$mode: $src compiles ($(head -3 "$T/err"))"; }
		done
	done
	# shellcheck disable=SC2086
	$CC $F -Werror -DZYPEROS_KIT_BUILD $SDL -c -o "$T/kit/brick_kit.o" src/device/brick_kit.c 2> "$T/err" ||
		ck 1 "kit: src/device/brick_kit.c compiles ($(head -3 "$T/err"))"
	for o in "$T"/rel/*.o; do
		! { nm "$o" 2> /dev/null | grep -Eq "$KIT_SYMBOLS" || strings -a "$o" | grep -Eq "$KIT_STRINGS"; }
		ck $? "a release's $(basename "$o"): no kit symbol or string"
	done
	nm "$T/kit/about_screen.o" | grep -q 'kit_page'
	ck $? "a kit's about_screen.o calls its page"
	nm "$T/kit/brick_hw.o" | grep -q 'kit_hw_facts'
	ck $? "a kit's brick_hw.o reads its facts"
	strings -a "$T/kit/hwreport.o" | grep -q 'PMIC registers'
	ck $? "a kit's hwreport.o prints the PMIC's registers"
	strings -a "$T/kit/hwreport.o" | grep -q 'LED facts'
	ck $? "a kit's hwreport.o prints the lights' facts"
	# Defined: T, or W where src/kit.h's weak declaration makes the definition
	# weak too (GNU nm; the Mac's says T)
	nm "$T/kit/brick_kit.o" | grep -Eq '[TW] _?kit_page$' && nm "$T/kit/brick_kit.o" | grep -Eq '[TW] _?kit_hw_facts$' &&
		strings -a "$T/kit/brick_kit.o" | grep -q 'Hardware Check'
	ck $? "a kit's brick_kit.o has its page and its facts"
	# The Brick's profile, as devices-check builds every profile
	cat > "$T/prof.c" << 'EOF'
#include "device/profile.h"
#include <stdio.h>
extern const device_profile device_profile_brick;
int main(void)
{
	printf("%d %d\n", device_profile_brick.gpu_shaders, device_profile_brick.gpu_frames);
	return 0;
}
EOF
	for mode in rel kit; do
		K=
		[ "$mode" = kit ] && K=-DZYPEROS_KIT_BUILD
		# shellcheck disable=SC2086
		$CC -std=gnu11 -D_GNU_SOURCE -O1 -w -Isrc $K -DZYPEROS_PROFILE=device_profile_desktop -DZYPEROS_PROFILES_ALL \
			-o "$T/prof-$mode" "$T/prof.c" src/profile.c src/thermal.c src/ds/button.c \
			${DEVICE_PROFILES:-} ${DEVICE_BUILDS:-} 2> "$T/err" ||
			{ ck 1 "$mode: the profiles link ($(head -3 "$T/err"))"; continue; }
		got=$("$T/prof-$mode")
		# on in a release too from 1.1.0 (it was kit-only before)
		[ "$got" = "1 1" ]
		ck $? "a $mode build's Brick: GPU shaders and GPU frames on (got $got)"
	done
else
	echo "  skipped: the hooks' files need SDL2's headers (pkg-config sdl2 SDL2_image SDL2_ttf)"
fi
if $CC -std=gnu11 -Wall -Wextra -Werror -O1 -DZYPEROS_KIT_BUILD -o "$T/hwreport-check" \
	tools/hwreport-check.c src/hwreport.c 2> "$T/err"; then
	"$T/hwreport-check" > "$T/out" 2>&1
	ck $? "a kit's hardware report: tools/hwreport-check.c as a kit's ($(tail -1 "$T/out"))"
else
	ck 1 "a kit's tools/hwreport-check.c compiles ($(head -3 "$T/err"))"
fi

# ---- 3. the host launcher, never a kit ---------------------------------------
if [ -x build-native/zyperos ]; then
	! { nm build-native/zyperos 2> /dev/null | grep -Eq "$KIT_SYMBOLS" ||
		strings -a build-native/zyperos | grep -Eq "$KIT_STRINGS"; }
	ck $? "build-native/zyperos: no kit symbol or string"
else
	echo "  skipped: no build-native/zyperos (make native)"
fi

# ---- 4. no start script seeds Diatom's present file ---------------------------
! grep -l 'diatom-present' sd/*/device.sh > /dev/null 2>&1
ck $? "no sd/*/device.sh seeds diatom-present"

# ---- 5. the present file's place, the same on both sides ----------------------
cat > "$T/present.c" << 'EOF'
#include "present_file.h"
int main(void)
{
	char d[512], f[600];

	present_file_paths(d, sizeof d, f, sizeof f);
	printf("%s %s\n", d, f);
	return 0;
}
EOF
if $CC -std=gnu11 -Wall -Wextra -Werror -O1 -Idiatom/port -o "$T/present" "$T/present.c" 2> "$T/err"; then
	got=$(env -u USERDATA_PATH "$T/present")
	[ "$got" = "/mnt/SDCARD/.userdata/brick /mnt/SDCARD/.userdata/brick/diatom-present" ]
	ck $? "no USERDATA_PATH: the Brick's folder, as before (got $got)"
	got=$(USERDATA_PATH= "$T/present")
	[ "$got" = "/mnt/SDCARD/.userdata/brick /mnt/SDCARD/.userdata/brick/diatom-present" ]
	ck $? "an empty USERDATA_PATH: the Brick's folder (got $got)"
	got=$(USERDATA_PATH=/mnt/SDCARD/.userdata/smartpro "$T/present")
	[ "$got" = "/mnt/SDCARD/.userdata/smartpro /mnt/SDCARD/.userdata/smartpro/diatom-present" ]
	ck $? "a Smart Pro's USERDATA_PATH: its own folder (got $got)"
else
	ck 1 "diatom/port/present_file.h compiles ($(head -3 "$T/err"))"
fi
grep -q 'present_file_paths' src/device/brick_kit.c && grep -q 'present_file_paths' diatom/port/brick.c &&
	! grep -q '"/mnt/SDCARD/[.]userdata/brick' src/device/brick_kit.c
ck $? "the kit's seed and Diatom's Brick port both take the file from present_file.h"
grep -q 'USERDATA_PATH=$SDCARD/.userdata/$DEVICE_ID' sd/brick/device.sh
ck $? "sd/brick/device.sh names USERDATA_PATH after the device, as present_file.h reads it"

if [ "$fails" -ne 0 ]; then
	echo "kitbuild: $fails of $checks checks failed"
	exit 1
fi
echo "kitbuild: $checks checks passed: a release has none of a kit's code"
