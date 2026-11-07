#!/bin/sh
# System Update end to end, on this computer (make check-update):
#
#   two fake releases packed and signed with a throwaway key (tools/
#   update-pack.py, tools/update-key.sh), a fake card at the first, then
#   zyperos-update check and download from a file:// source and the boot
#   switch (update/apply.sh) as the device runs them. Covered: only changed
#   files download; low battery refuses; a download cut short goes on from
#   where it stopped; a changed list, a list signed by another key and a
#   changed pack are all refused; the switch keeps the launcher's state; the
#   step back and forward; a version that does not start goes back by itself;
#   a switch cut between its renames is finished; a release that needs a
#   newer system image says so.
#
# Needs OpenSSL 3 or newer (Ed25519 signing) and the libcrypto, libcurl and
# zlib the program links; skips without them.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN=$ROOT/build-native/zyperos-update
T=$ROOT/build-native/update-check
fails=0

if ! pkg-config --exists libcrypto libcurl zlib 2>/dev/null; then
	echo "skipped: no libcrypto, libcurl or zlib here"
	exit 0
fi
case $(openssl version 2>/dev/null) in
"OpenSSL "[3-9]*) ;;
*) echo "skipped: no OpenSSL 3 here for Ed25519"; exit 0 ;;
esac
[ -x "$BIN" ] || { echo "FAIL no $BIN: make build-native/zyperos-update first" >&2; exit 1; }

ok()   { echo "  ok   $*"; }
bad()  { echo "  FAIL $*"; fails=$((fails + 1)); }
expect() { # expect WHAT ACTUAL WANTED
	if [ "$2" = "$3" ]; then ok "$1"; else bad "$1: got '$2', wanted '$3'"; fi
}
state() { sed -n 's/^state //p' "$CARD/.userdata/pixel2/update/status"; }
field() { sed -n "s/^$1 //p" "$CARD/.userdata/pixel2/update/status"; }

rm -rf "$T"
mkdir -p "$T"
export ZYPEROS_UPDATE_KEYS=$T/keys
export ZYPEROS_UPDATE_SYSTEM=$T/system
export ZYPEROS_UPDATE_BATTERY="80 0"
KEY=$T/key.pem
sh "$ROOT/tools/update-key.sh" new "$KEY" > /dev/null || { echo "FAIL the throwaway key" >&2; exit 1; }
printf 'version 0.1.0\nupdate 1\n' > "$T/system"

# Bytes that do not compress (a stored member) and bytes that do (a gzip one)
noise() { openssl rand "$1" > "$2"; }
words() { i=0; : > "$2"; while [ $i -lt "$1" ]; do echo "line $i of a file that compresses well" >> "$2"; i=$((i + 1)); done; }

# Release 1 and release 2, laid out as mk/payload.sh lays out a card
mkrel() { # mkrel DIR VERSION
	P=$1/ZyperOS
	mkdir -p "$P/cores" "$P/res/web" "$P/cards/classic" "$1/Licenses" "$1/Roms/NES" "$1/Roms/Game Boy"
	echo "$2" > "$P/VERSION"
	echo "#!/bin/sh" > "$P/launch.sh"
	: > "$P/res/empty"
	echo "same in both" > "$P/res/my file.txt"
	words 4000 "$P/cores/a_libretro.so"
	echo "licenses $2" > "$1/Licenses/ZyperOS.md"
}
mkrel "$T/r1" 0.1.0
noise 300000 "$T/r1/ZyperOS/zyperos.elf"
noise 4000 "$T/r1/ZyperOS/res/old.png"
mkrel "$T/r2" 0.2.0
# A new system, with the empty .media an older payload made beside it
mkdir -p "$T/r2/Roms/Dreamcast/.media"
noise 310000 "$T/r2/ZyperOS/zyperos.elf"
noise 2000000 "$T/r2/ZyperOS/cores/b_libretro.so"
words 3000 "$T/r2/ZyperOS/cores/c_libretro.so"

pack() { # pack DIR VERSION OUT [min-system]
	python3 "$ROOT/tools/update-pack.py" --payload "$1/ZyperOS" --card-extra "$1" --version "$2" \
		--device pixel2 --out "$3" --min-system "${4:-0.1.0}" --notes "$T/notes.md" > /dev/null &&
	sh "$ROOT/tools/update-key.sh" sign "$3/ZyperOS-pixel2-update.txt.unsigned" \
		"$3/ZyperOS-pixel2-update.txt" "$KEY" > /dev/null
}
printf '# Notes\n\n- A new **home screen**\n- [Rumble](https://example.com) on the Dreamcast\n' > "$T/notes.md"
pack "$T/r1" 0.1.0 "$T/rel1" || { echo "FAIL packing release 1" >&2; exit 1; }
pack "$T/r2" 0.2.0 "$T/rel2" || { echo "FAIL packing release 2" >&2; exit 1; }
PACK2=$T/rel2/ZyperOS-v0.2.0-pixel2-update.zpk

# The card: release 1 installed, with its index as payload.sh writes it,
# and the launcher's state in its folder
newcard() {
	CARD=$T/card
	rm -rf "$CARD"
	mkdir -p "$CARD"
	cp -R "$T/r1/ZyperOS" "$T/r1/Licenses" "$T/r1/Roms" "$CARD/"
	(cd "$CARD/ZyperOS" && find . -type f ! -name '.*' | sed 's|^\./||' | sort | while IFS= read -r f; do
		printf '%s %s %s\n' "$(shasum -a 256 "$f" | cut -c1-64)" "$(wc -c < "$f" | tr -d ' ')" "$f"
	done) > "$CARD/ZyperOS/.files"
	echo "SNES|Chrono Trigger" > "$CARD/ZyperOS/.last"
}
run() { "$BIN" "$@" --card "$CARD" --device pixel2 > "$T/out" 2>&1; }
src() { echo "--source"; echo "file://$1/"; }
apply() { sh "$ROOT/update/apply.sh" "$CARD" pixel2 > /dev/null 2>&1; }
same_tree() { # same_tree A B: the same files, byte for byte (dotfiles aside)
	diff -r -x '.*' "$1" "$2" > /dev/null 2>&1
}

echo "check-update:"
newcard

# --- check ------------------------------------------------------------------
run check --source "file://$T/rel2/"
expect "check finds 0.2.0" "$?:$(state):$(field version)" "0:available:0.2.0"
expect "  ...and keeps the list, What's New in it" "$(grep -c '^note ' "$CARD/.userdata/pixel2/update/latest.txt")" "2"
dl=$(field download)
full=$(sed -n 's/^pack [^ ]* \([0-9]*\) .*/\1/p' "$T/rel2/ZyperOS-pixel2-update.txt")
if [ "${dl:-0}" -gt 0 ] && [ "$dl" -lt "$full" ]; then ok "only changed files count ($dl of $full bytes)"; else bad "download size $dl of $full"; fi

run check --source "file://$T/rel1/"
expect "the installed release is up to date" "$?:$(state)" "1:uptodate"

# --- refused lists ------------------------------------------------------------
mkdir -p "$T/tampered"
cp "$PACK2" "$T/tampered/"
sed 's/^version 0.2.0$/version 0.2.1/' "$T/rel2/ZyperOS-pixel2-update.txt" > "$T/tampered/ZyperOS-pixel2-update.txt"
run check --source "file://$T/tampered/"
expect "a changed list is refused" "$?:$(state)" "2:error"
grep -q 'signature does not match' "$CARD/.userdata/pixel2/update/status" && ok "  ...for its signature" || bad "  ...not for its signature"

mkdir -p "$T/otherkey"
openssl genpkey -algorithm ed25519 -out "$T/other.pem" 2> /dev/null
mkdir -p "$T/otherkeys"
ZYPEROS_UPDATE_KEYS=$T/otherkeys sh "$ROOT/tools/update-key.sh" new "$T/other2.pem" > /dev/null
ZYPEROS_UPDATE_KEYS=$T/otherkeys sh "$ROOT/tools/update-key.sh" sign "$T/rel2/ZyperOS-pixel2-update.txt.unsigned" \
	"$T/otherkey/ZyperOS-pixel2-update.txt" "$T/other2.pem" > /dev/null
cp "$PACK2" "$T/otherkey/"
run check --source "file://$T/otherkey/"
expect "a list signed by an unknown key is refused" "$?:$(state)" "2:error"

pack "$T/r2" 0.2.0 "$T/relsys" 9.0.0
run check --source "file://$T/relsys/"
expect "a release for a newer system image says so" "$?:$(state)" "2:needs-system"

# --- battery ----------------------------------------------------------------
(export ZYPEROS_UPDATE_BATTERY="18 0"; run download --source "file://$T/rel2/")
expect "18% without the charger refuses" "$?:$(state)" "2:battery"
[ -d "$CARD/ZyperOS.next" ] && bad "  ...and staged something" || ok "  ...and staged nothing"
(export ZYPEROS_UPDATE_BATTERY="18 1"; run check --source "file://$T/rel2/")

# --- cut short, then on from there -------------------------------------------
# The pack cut in the middle of b_libretro.so's member
line=$(grep ' cores/b_libretro.so$' "$T/rel2/ZyperOS-pixel2-update.txt")
boff=$(echo "$line" | cut -d' ' -f4)
bst=$(echo "$line" | cut -d' ' -f5)
cut=$((boff + bst / 2))
mkdir -p "$T/cut"
cp "$T/rel2/ZyperOS-pixel2-update.txt" "$T/cut/"
head -c "$cut" "$PACK2" > "$T/cut/ZyperOS-v0.2.0-pixel2-update.zpk"
run download --source "file://$T/cut/"
expect "a download cut short stops" "$?:$(state)" "2:stopped"
part=$(wc -c < "$CARD/ZyperOS.next/cores/b_libretro.so.zpart" 2>/dev/null | tr -d ' ')
if [ "${part:-0}" -gt 0 ]; then ok "  ...keeping what came ($part bytes of the member)"; else bad "  ...kept nothing"; fi
[ -f "$CARD/ZyperOS.next/.ready" ] && bad "  ...but says ready" || ok "  ...and is not ready"
run download --source "file://$T/rel2/"
expect "the download goes on and finishes" "$?:$(state)" "0:ready"
grep -q "^0.2.0$" "$CARD/ZyperOS.next/.ready" && ok "  ...marked ready for 0.2.0" || bad "  ...no .ready"
same_tree "$CARD/ZyperOS.next" "$T/r2/ZyperOS" && ok "  ...and the staged folder is release 2" || bad "  ...staged folder differs from release 2"
[ -f "$CARD/ZyperOS.next/.files" ] && ok "  ...with its index" || bad "  ...without an index"
cmp -s "$CARD/ZyperOS/zyperos.elf" "$T/r1/ZyperOS/zyperos.elf" && ok "  ...and the running folder untouched" || bad "  ...the running folder changed"

run check --source "file://$T/rel2/"
expect "check after: ready" "$?:$(state)" "0:ready"

# --- over the USB cable: the files the page brought (docs/updates.md) ----------
# The launcher keeps them in the state folder's cable/ under their release
# names, and runs the program on them as on any release (main.c, xfer_update)
newcard
CABLE=$CARD/.userdata/pixel2/update/cable
mkdir -p "$CABLE"
cp "$T/rel2/ZyperOS-pixel2-update.txt" "$PACK2" "$CABLE/"
run check --source "file://$CABLE/"
expect "over the cable: check finds 0.2.0" "$?:$(state):$(field version)" "0:available:0.2.0"
run download --source "file://$CABLE/"
expect "  ...and stages it" "$?:$(state)" "0:ready"
same_tree "$CARD/ZyperOS.next" "$T/r2/ZyperOS" && ok "  ...release 2, whole" || bad "  ...staged folder differs from release 2"
newcard
mkdir -p "$CABLE"
cp "$T/tampered/ZyperOS-pixel2-update.txt" "$PACK2" "$CABLE/"
run check --source "file://$CABLE/"
expect "  ...a list changed on the computer is refused" "$?:$(state)" "2:error"

# --- a bad pack: the file is fetched again, then refused ----------------------
newcard
mkdir -p "$T/badpack"
cp "$T/rel2/ZyperOS-pixel2-update.txt" "$T/badpack/"
cp "$PACK2" "$T/badpack/"
python3 - "$T/badpack/ZyperOS-v0.2.0-pixel2-update.zpk" "$boff" <<'EOF'
import sys
p, off = sys.argv[1], int(sys.argv[2]) + 1000
with open(p, "r+b") as f:
    f.seek(off); b = f.read(1); f.seek(off); f.write(bytes([b[0] ^ 0xff]))
EOF
run download --source "file://$T/badpack/"
expect "a changed pack is refused" "$?:$(state)" "2:error"
[ -f "$CARD/ZyperOS.next/cores/b_libretro.so" ] && bad "  ...but the bad file is staged" || ok "  ...and the bad file is not staged"
run download --source "file://$T/rel2/"
expect "  ...and the good pack finishes it" "$?:$(state)" "0:ready"

# --- a member that inflates past its listed size -----------------------------
# Signed with the trusted (throwaway) key, so only the size check stops it
newcard
mkdir -p "$T/bomb"
cp "$PACK2" "$T/bomb/"
sed 's|^\(file [0-9a-f]* \)[0-9]*\( .* g cores/c_libretro.so\)$|\110\2|' \
	"$T/rel2/ZyperOS-pixel2-update.txt.unsigned" > "$T/bomb/list.unsigned"
grep -q ' 10 .* g cores/c_libretro.so$' "$T/bomb/list.unsigned" || bad "the bomb list was not made"
sh "$ROOT/tools/update-key.sh" sign "$T/bomb/list.unsigned" "$T/bomb/ZyperOS-pixel2-update.txt" "$KEY" > /dev/null
run download --source "file://$T/bomb/"
expect "a member bigger than its listed size is refused" "$?:$(state)" "2:error"
big=$(find "$CARD/ZyperOS.next" -name 'c_libretro.so*' -size +1k 2>/dev/null | head -1)
[ -z "$big" ] && ok "  ...and nothing of it is left on the card" || bad "  ...$big is left"
newcard
run download --source "file://$T/rel2/"

# --- the switch at the next start --------------------------------------------
apply
expect "the switch: 0.2.0 runs" "$(cat "$CARD/ZyperOS/VERSION")" "0.2.0"
expect "  ...0.1.0 kept for a step back" "$(cat "$CARD/ZyperOS.prev/VERSION" 2>/dev/null)" "0.1.0"
[ -d "$CARD/ZyperOS.next" ] && bad "  ...ZyperOS.next left" || ok "  ...nothing left staged"
same_tree "$CARD/ZyperOS" "$T/r2/ZyperOS" && ok "  ...the folder is release 2" || bad "  ...the folder differs from release 2"
expect "  ...the launcher's state came along" "$(cat "$CARD/ZyperOS/.last" 2>/dev/null)" "SNES|Chrono Trigger"
expect "  ...on trial, first start counted" "$(cat "$CARD/ZyperOS/.trial" 2>/dev/null)" "1"
expect "  ...the card's licenses updated" "$(cat "$CARD/Licenses/ZyperOS.md")" "licenses 0.2.0"
[ -d "$CARD/Roms/Dreamcast" ] && ok "  ...a new system's Roms folder made" || bad "  ...no Roms/Dreamcast"
# The image's empty .media is never an update's to make (docs/updates.md)
[ -e "$CARD/Roms/Dreamcast/.media" ] && bad "  ...an empty .media made by the update" || ok "  ...no empty .media made"
expect "  ...result" "$(cat "$CARD/.userdata/pixel2/update/result")" "updated 0.1.0 0.2.0"
[ -e "$CARD/.userdata/pixel2/update/status" ] && bad "  ...the old status (ready) is left" || ok "  ...the old status (ready) is gone"
run check --source "file://$T/rel2/"
expect "  ...and check says up to date" "$?:$(state)" "1:uptodate"

# --- a start a charger made counts nothing ------------------------------------
echo "root=/dev/mmcblk0p2 zyperos.slot=a zyperos.trial=0 zycharge=1" > "$T/charge-cmdline"
(export ZYPEROS_UPDATE_CMDLINE=$T/charge-cmdline; apply)
expect "a charger's start is not counted" "$(cat "$CARD/ZyperOS/.trial")" "1"
echo "root=/dev/mmcblk0p2 zyperos.slot=a zycharge=10" > "$T/charge-cmdline"
(export ZYPEROS_UPDATE_CMDLINE=$T/charge-cmdline; apply)
expect "  ...a word that only looks like it is" "$(cat "$CARD/ZyperOS/.trial")" "2"
echo 1 > "$CARD/ZyperOS/.trial"

# --- a version that does not start ------------------------------------------
apply
expect "a second start without the launcher's word" "$(cat "$CARD/ZyperOS/.trial")" "2"
apply
expect "the third goes back by itself" "$(cat "$CARD/ZyperOS/VERSION")" "0.1.0"
expect "  ...and says why" "$(cat "$CARD/.userdata/pixel2/update/result")" "failed 0.2.0"
[ -e "$CARD/.userdata/pixel2/update/status" ] && bad "  ...a status is left" || ok "  ...no stale status"
[ -e "$CARD/ZyperOS/.trial" ] && bad "  ...still on trial" || ok "  ...not on trial"
expect "  ...the launcher's state kept" "$(cat "$CARD/ZyperOS/.last" 2>/dev/null)" "SNES|Chrono Trigger"

# --- a step back asked for, and forward again ---------------------------------
newcard
run download --source "file://$T/rel2/"
apply
rm -f "$CARD/ZyperOS/.trial"          # the launcher's word
run rollback
expect "a step back is asked for" "$?:$(state):$(field version)" "0:rollback:0.1.0"
apply
expect "  ...and taken: 0.1.0 runs" "$(cat "$CARD/ZyperOS/VERSION")" "0.1.0"
expect "  ...0.2.0 kept for a step forward" "$(cat "$CARD/ZyperOS.prev/VERSION")" "0.2.0"
expect "  ...result" "$(cat "$CARD/.userdata/pixel2/update/result")" "rolled-back 0.2.0"
run rollback
apply
expect "the step forward again" "$(cat "$CARD/ZyperOS/VERSION")" "0.2.0"
printf 'version 0.2.0\nupdate 1\n' > "$T/system"
run rollback
expect "never back past the system image's own version" "$?:$(state)" "2:error"
printf 'version 0.1.0\nupdate 1\n' > "$T/system"

# --- a switch cut between its renames --------------------------------------
newcard
run download --source "file://$T/rel2/"
mv "$CARD/ZyperOS" "$CARD/ZyperOS.prev"          # cut after the first rename
apply
expect "a cut switch is finished at the next start" "$(cat "$CARD/ZyperOS/VERSION")" "0.2.0"
expect "  ...with 0.1.0 kept" "$(cat "$CARD/ZyperOS.prev/VERSION")" "0.1.0"

# --- an image without System Update -----------------------------------------
(export ZYPEROS_UPDATE_SYSTEM=$T/nothing; run check --source "file://$T/rel2/")
expect "an image without System Update says so" "$?:$(state)" "2:unsupported"

# --- system slots (layer 2) -------------------------------------------------
# A card with two system slots, started from slot a: files stand for root A
# and root B (partitions 2 and 4), a folder for the boot partition, a file
# for /proc/cmdline
SL=$T/slots
mkdir -p "$SL/boot/a" "$SL/sys2/boot"
head -c 3000000 /dev/zero > "$SL/p2"
head -c 3000000 /dev/zero > "$SL/p4"
printf 'slot=a\ntrial=0\ntry=0\nfell=-\na=0.1.0\nb=-\n' > "$SL/boot/bootstate.txt"
echo "root=/dev/vda2 zyperos.slot=a zyperos.trial=0" > "$SL/cmdline"
noise 2000000 "$SL/sys2/root.squashfs"
noise 300000 "$SL/sys2/boot/Image"
noise 5000 "$SL/sys2/boot/board.dtb"
python3 "$ROOT/tools/update-pack.py" --kind system --payload "$SL/sys2" --version 0.2.0 \
	--device pixel2 --out "$T/rel2" > /dev/null &&
sh "$ROOT/tools/update-key.sh" sign "$T/rel2/ZyperOS-pixel2-system.txt.unsigned" \
	"$T/rel2/ZyperOS-pixel2-system.txt" "$KEY" > /dev/null || bad "packing the system"
slots_env() {
	export ZYPEROS_UPDATE_CMDLINE=$SL/cmdline ZYPEROS_UPDATE_BOOT=$SL/boot
	export ZYPEROS_UPDATE_PART2=$SL/p2 ZYPEROS_UPDATE_PART4=$SL/p4
}
bootstate() { sed -n "s/^$1=//p" "$SL/boot/bootstate.txt"; }
slots_env
newcard
run check --source "file://$T/rel2/"
expect "slots: a newer system comes first" "$?:$(state):$(field kind):$(field version):$(field slot)" "0:available:system:0.2.0:a"
run download --source "file://$T/rel2/"
expect "slots: the system downloads and goes into slot b" "$?:$(state)" "0:ready"
head -c 2000000 "$SL/p4" | cmp -s - "$SL/sys2/root.squashfs" && ok "  ...root B holds the new root" || bad "  ...root B is not the new root"
head -c 2000000 "$SL/p2" | cmp -s - "$SL/sys2/root.squashfs" && bad "  ...root A was written" || ok "  ...root A, the running one, untouched"
cmp -s "$SL/sys2/boot/Image" "$SL/boot/b/Image" && cmp -s "$SL/sys2/boot/board.dtb" "$SL/boot/b/board.dtb" &&
	ok "  ...the kernel and device tree in /b" || bad "  .../b on the boot partition is wrong"
expect "  ...bootstate: slot b on trial, its version" "$(bootstate slot):$(bootstate trial):$(bootstate try):$(bootstate b):$(bootstate a)" "b:1:0:0.2.0:0.1.0"
[ -d "$CARD/.zyperos-system" ] && bad "  ...the staging is left" || ok "  ...the staging is gone"
run check --source "file://$T/rel2/"
expect "  ...check: ready, restart" "$?:$(state):$(field kind)" "0:ready:system"

# The new system starts on trial (U-Boot counted one), and the launcher confirms
echo "root=/dev/vda4 zyperos.slot=b zyperos.trial=1" > "$SL/cmdline"
sed -i.bak 's/^try=.*/try=1/' "$SL/boot/bootstate.txt"
run check --source "file://$T/rel2/" > /dev/null
run confirm
expect "slots: confirm keeps the new system" "$?:$(bootstate slot):$(bootstate trial):$(bootstate try)" "0:b:0:0"
[ -e "$CARD/.userdata/pixel2/update/status" ] && bad "  ...the status from before the restart is left" || ok "  ...and drops the status from before the restart"
printf 'version 0.2.0\nupdate 1\n' > "$T/system"
run check --source "file://$T/rel2/"
expect "  ...and the system is up to date, the check goes on to the folder" "$(field kind):$(field version):$(field other)" ":0.2.0:0.1.0"

# Roll Back to the system in the other slot
run rollback
expect "slots: Roll Back goes to slot a's system" "$?:$(state):$(field version):$(bootstate slot):$(bootstate trial)" "0:rollback:0.1.0:a:0"

# U-Boot gave up on slot b and started slot a: the next confirm says so
printf 'slot=a\ntrial=0\ntry=0\nfell=b\na=0.1.0\nb=0.2.0\n' > "$SL/boot/bootstate.txt"
echo "root=/dev/vda2 zyperos.slot=a zyperos.trial=0" > "$SL/cmdline"
printf 'version 0.1.0\nupdate 1\n' > "$T/system"
printf 'state ready\nversion 0.2.0\nkind system\n' > "$CARD/.userdata/pixel2/update/status"
run confirm
expect "slots: a system U-Boot gave up on is said" "$(cat "$CARD/.userdata/pixel2/update/result"):$(bootstate fell)" "failed 0.2.0:-"
[ -e "$CARD/.userdata/pixel2/update/status" ] && bad "  ...the ready from before is left" || ok "  ...and the ready from before is gone"
expect "  ...its slot holds no system to roll back to" "$(bootstate b):$(bootstate a)" "-:0.1.0"
run check --source "file://$T/rel2/"
expect "  ...so the page offers none" "$(field kind):$(field other)" "system:"

# A system going into the other slot takes that slot's system off first, and
# the folder that came with it: a cut write leaves no Roll Back there. A Roll
# Back waiting for a restart is off: the running slot starts next.
printf 'slot=b\ntrial=0\ntry=0\nfell=-\na=0.1.0\nb=0.1.5\n' > "$SL/boot/bootstate.txt"
mkdir -p "$CARD/ZyperOS.prev" && echo 0.1.5 > "$CARD/ZyperOS.prev/VERSION"
echo 0.1.5 > "$CARD/.userdata/pixel2/update/rollback"
cp "$SL/p4" "$SL/p4.full" && head -c 1000000 /dev/zero > "$SL/p4"
run download --source "file://$T/rel2/"
expect "slots: a cut install leaves no system in slot b, slot a next" "$(state):$(bootstate slot):$(bootstate b):$(bootstate a):$(bootstate trial)" "error:a:-:0.1.0:0"
[ -e "$CARD/ZyperOS.prev" ] && bad "  ...slot b's folder is left" || ok "  ...and slot b's folder went with it"
[ -e "$CARD/.userdata/pixel2/update/rollback" ] && bad "  ...the Roll Back is left" || ok "  ...and the waiting Roll Back is off"
mv "$SL/p4.full" "$SL/p4"
run check --source "file://$T/rel2/"
expect "  ...check: the staged system is still to install, not ready" "$?:$(state):$(field kind)" "0:available:system"
run download --source "file://$T/rel2/"
expect "  ...Download again finishes" "$?:$(state):$(bootstate slot):$(bootstate b):$(bootstate trial)" "0:ready:b:0.2.0:1"

# A system bigger than the card's slots (a card laid out with smaller ones)
# is said before anything is fetched, and the folder's check stays out of it
cp "$SL/p4" "$SL/p4.full" && head -c 1000000 /dev/zero > "$SL/p4"
printf 'slot=a\ntrial=0\ntry=0\nfell=-\na=0.1.0\nb=-\n' > "$SL/boot/bootstate.txt"
run check --source "file://$T/rel2/"
expect "slots: a system too big for the slots is refused" "$?:$(state):$(field version)" "2:too-big:0.2.0"
grep -q "does not fit this card's system slots (0 MB, it needs 2 MB)" "$CARD/.userdata/pixel2/update/status" &&
	ok "  ...saying why, and the installer" || bad "  ...not said: $(field message)"
mv "$SL/p4.full" "$SL/p4"

# A gzip member whose first 64 KB of input inflates to exactly 64 KB: the
# input runs out just as the output buffer fills, and zlib's next call says
# Z_BUF_ERROR, which is no error. Found 2026-11-06 at 59 MB into a 331 MB
# root (LZ4 squashfs packs to mostly stored blocks); made here on purpose
mkdir -p "$SL/sys4/boot"
noise 5000 "$SL/sys4/boot/Image"
python3 - "$SL/sys4/root.squashfs" "$T/member.gz" <<'PY'
import random, struct, sys, zlib
# Y zeros, deflated and flushed to a byte, then stored blocks, so that the
# stored data starts at input byte Y: 64 KB in is then 64 KB out
for y in range(16, 4096):
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    head = c.compress(bytes(y)) + c.flush(zlib.Z_SYNC_FLUSH)
    if y == 10 + len(head) + 5:
        break
else:
    sys.exit("no length works")
data = random.Random(7).randbytes(3 * 65536)
body, rest = bytearray(head), data
while rest:
    part, rest = rest[:65535], rest[65535:]
    body += bytes([0 if rest else 1]) + struct.pack("<HH", len(part), len(part) ^ 0xFFFF) + part
content = bytes(y) + data
open(sys.argv[1], "wb").write(content)
open(sys.argv[2], "wb").write(b"\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\xff" + bytes(body) +
                              struct.pack("<II", zlib.crc32(content), len(content)))
PY
python3 "$ROOT/tools/update-pack.py" --kind system --payload "$SL/sys4" --version 0.3.0 \
	--device pixel2 --out "$T/rel4" > /dev/null || bad "packing the system"
python3 - "$T/rel4" "$T/member.gz" <<'PY'
import glob, hashlib, os, sys
# The pack's root member swapped for the made one (root comes last)
rel, member = sys.argv[1], open(sys.argv[2], "rb").read()
lst = os.path.join(rel, "ZyperOS-pixel2-system.txt.unsigned")
pack = glob.glob(os.path.join(rel, "*.zpk"))[0]
lines = open(lst).read().split("\n")
for i, l in enumerate(lines):
    w = l.split(" ")
    if w[0] == "file" and w[-1] == "root.squashfs":
        off = int(w[3])
        w[4], w[5] = str(len(member)), "g"
        lines[i] = " ".join(w)
data = open(pack, "rb").read()[:off] + member
open(pack, "wb").write(data)
for i, l in enumerate(lines):
    if l.startswith("pack "):
        lines[i] = "pack %s %d %s" % (os.path.basename(pack), len(data), hashlib.sha256(data).hexdigest())
open(lst, "w").write("\n".join(lines))
PY
sh "$ROOT/tools/update-key.sh" sign "$T/rel4/ZyperOS-pixel2-system.txt.unsigned" \
	"$T/rel4/ZyperOS-pixel2-system.txt" "$KEY" > /dev/null || bad "signing the system"
run check --source "file://$T/rel4/"
run download --source "file://$T/rel4/"
expect "slots: a member that fills the output just as its input runs out" "$?:$(state):$(bootstate b)" "0:ready:0.3.0"
head -c "$(wc -c < "$SL/sys4/root.squashfs" | tr -d ' ')" "$SL/p4" | cmp -s - "$SL/sys4/root.squashfs" &&
	ok "  ...and root B holds it" || bad "  ...root B is not the system"

# A folder's list where the system's belongs is refused, and the folder's
# check goes on
cp "$T/rel2/ZyperOS-pixel2-update.txt" "$T/rel2/ZyperOS-pixel2-system.txt"
newcard
run check --source "file://$T/rel2/"
expect "slots: a folder list as the system's is refused; the folder is offered" "$?:$(state):$(field kind):$(field version)" "0:available::0.2.0"
grep -q 'a folder list where a system list belongs' "$CARD/.userdata/pixel2/update/log" && ok "  ...for its kind" || bad "  ...not for its kind"
unset ZYPEROS_UPDATE_CMDLINE ZYPEROS_UPDATE_BOOT ZYPEROS_UPDATE_PART2 ZYPEROS_UPDATE_PART4

if [ $fails -gt 0 ]; then
	echo "check-update: $fails failed (files in $T)"
	exit 1
fi
rm -rf "$T"
echo "check-update: all passed"
