#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# make check-core-stamp: mk/core-stamp.sh on a scratch vendor/ of made-up
# cores. Every patched core stamped from the patches here passes; then each
# way a core goes stale fails the check, and says what to run: a patch
# changed after the build (the 1.0.1 DS core, built before its language
# patch), a core changed after its stamp, a core with no stamp. A core that
# is not in vendor/ is passed over (mk/payload.sh has its own check), adopt
# stamps only a core whose sha256 its build script pins, and every patch in
# mk/patches belongs to a core in the table.
set -u
cd "$(dirname "$0")/.."

D=build-native/core-stamp
fails=0
ok() { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fails=$((fails + 1)); }

rm -rf "$D"
mkdir -p "$D/root/mk/patches" "$D/vendor/cores" || exit 1
# The script and real copies of the patches, in a tree of their own, so a
# patch can be changed without touching the repository's
cp mk/core-stamp.sh "$D/root/mk/" || exit 1
cp mk/patches/*.patch "$D/root/mk/patches/" || exit 1
for b in build-dsperate.sh build-fake08.sh build-flycast2021.sh build-freej2me.sh build-mupen64plus-next.sh build-parallel-n64.sh; do
	cp "mk/$b" "$D/root/mk/" || exit 1
done
S="sh $D/root/mk/core-stamp.sh"
V=$D/vendor
names=$($S list | sed -n 's/^\([a-z0-9_]*\) (mk\/.*$/\1/p')

# The table covers every patch in the repository
if sh mk/core-stamp.sh check "$D/empty-vendor" > "$D/out" 2>&1; then
	ok "every patch in mk/patches belongs to a core in the table"
else
	bad "a patch in mk/patches belongs to no core: $(cat "$D/out")"
fi

for n in $names; do
	echo "made-up $n" > "$V/cores/${n}_libretro.so"
	$S write "$n" "$V" > /dev/null || bad "write $n"
done
$S check "$V" > "$D/out" 2>&1 && ok "every core stamped from the patches here passes" ||
	bad "stamped cores fail: $(cat "$D/out")"
grep -q '^patch dsperate-lr-language.patch [0-9a-f]\{64\}$' "$V/cores/dsperate_libretro.stamp" &&
	ok "the DS core's stamp names its language patch, by sha256" || bad "the DS stamp lacks its language patch"

# A patch changed after the core was built
printf '\n' >> "$D/root/mk/patches/dsperate-lr-language.patch"
if $S check "$V" > "$D/out" 2>&1; then
	bad "a DS core built before its patch changed passes"
else
	grep -q 'dsperate_libretro.so is not the build of the patches here' "$D/out" &&
		grep -q 'run sh mk/build-dsperate.sh' "$D/out" &&
		ok "a DS core built before its patch changed fails, and says to build it" ||
		bad "the stale DS core's message: $(cat "$D/out")"
fi
cp mk/patches/dsperate-lr-language.patch "$D/root/mk/patches/"
$S check "$V" > /dev/null 2>&1 && ok "and passes once the patch is the one it was built with" ||
	bad "the restored patch still fails"

# A core changed after its stamp
echo "made-up fake08, built again by hand" > "$V/cores/fake08_libretro.so"
$S check "$V" > "$D/out" 2>&1 && bad "a core changed after its stamp passes" ||
	ok "a core changed after its stamp fails"
$S write fake08 "$V" > /dev/null

# No stamp
rm "$V/cores/flycast2021_libretro.stamp"
if $S check "$V" > "$D/out" 2>&1; then
	bad "a core with no stamp passes"
else
	grep -q 'flycast2021_libretro.so has no stamp' "$D/out" && ok "a core with no stamp fails" ||
		bad "the unstamped core's message: $(cat "$D/out")"
fi

# adopt: only a core whose sha256 the build script pins
if $S adopt flycast2021 -v "$V" > "$D/out" 2>&1; then
	bad "adopt stamps a core its build script does not make"
else
	ok "adopt refuses a core whose sha256 its build script does not pin"
fi
sha=$(shasum -a 256 "$V/cores/flycast2021_libretro.so" 2>/dev/null | cut -d' ' -f1)
[ -n "$sha" ] || sha=$(sha256sum "$V/cores/flycast2021_libretro.so" | cut -d' ' -f1)
sed -i.bak "s/^SHA=[0-9a-f]\{64\}/SHA=$sha/" "$D/root/mk/build-flycast2021.sh"
$S adopt flycast2021 -v "$V" > /dev/null 2>&1 && $S check "$V" > /dev/null 2>&1 &&
	ok "and stamps one it does, which then passes" || bad "adopt of a pinned core"

# A core not in vendor/ is passed over
rm "$V/cores/freej2me_plus_libretro.so" "$V/cores/freej2me_plus_libretro.stamp"
$S check "$V" > /dev/null 2>&1 && ok "a patched core that is not in vendor/ is passed over" ||
	bad "a missing core fails the stamp check"

# A patch that no core claims
: > "$D/root/mk/patches/newcore-fix.patch"
$S check "$V" > "$D/out" 2>&1 && bad "a patch no core claims passes" ||
	{ grep -q 'newcore-fix.patch belongs to no core' "$D/out" && ok "a patch no core claims fails" ||
	  bad "the unclaimed patch's message: $(cat "$D/out")"; }

if [ $fails -gt 0 ]; then
	echo "check-core-stamp: $fails failure(s)"
	exit 1
fi
echo "check-core-stamp: ok"
