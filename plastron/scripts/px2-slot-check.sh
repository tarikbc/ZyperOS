#!/bin/sh
# px2-push.sh's slot choice and bootstate.txt edits (scripts/px2-slot.sh),
# on the host: the slot not running is the one written, never the running
# one, and a card without a second slot, or a command line that does not add
# up, gets nothing. And the script the device runs parses, and writes no
# partition by number. ZyperOS's make check-px2-push.
set -u
cd "$(dirname "$0")"
. ./px2-slot.sh
fails=0
ok() { :; }
no() { echo "FAIL: $*" >&2; fails=$((fails + 1)); }

# The Pixel 2's real command line, as boot-slots.cmd builds it
line() { # slot root
	echo "root=/dev/mmcblk0p$2 rootwait ro console=ttyS2,1500000 zyperos.slot=$1 zyperos.trial=0 zytheme=beard zyzone=- zylang=en"
}
plan() { px2_slot_plan "$@" 2> /dev/null; }

[ "$(plan "$(line a 2)" 1 1)" = "a b mmcblk0p4" ] && ok || no "slot a running: not slot b in p4"
[ "$(plan "$(line b 4)" 1 1)" = "b a mmcblk0p2" ] && ok || no "slot b running: not slot a in p2"
# On trial is still the running slot
[ "$(plan "root=/dev/mmcblk0p4 rootwait zyperos.slot=b zyperos.trial=1" 1 1)" = "b a mmcblk0p2" ] && ok ||
	no "slot b on trial: not slot a"
# Never the running root: the kernel's slot and its root must agree
plan "$(line a 4)" 1 1 > /dev/null && no "slot a on p4 was planned"
plan "$(line b 2)" 1 1 > /dev/null && no "slot b on p2 was planned"
# Cards with one system, or no slot named: nothing
plan "$(line a 2)" 0 1 > /dev/null && no "no partition 4 was planned"
plan "$(line a 2)" 1 0 > /dev/null && no "no bootstate.txt was planned"
plan "root=/dev/mmcblk0p2 rootwait ro" 1 1 > /dev/null && no "a kernel that names no slot was planned"
plan "root=/dev/mmcblk0p2 zyperos.slot=c" 1 1 > /dev/null && no "slot c was planned"
plan "root=/dev/mmcblk0p3 zyperos.slot=a" 1 1 > /dev/null && no "a root on the games partition was planned"
plan "root=/dev/mmcblk0p22 zyperos.slot=a" 1 1 > /dev/null && no "p22 read as p2"
plan "xroot=/dev/mmcblk0p2 zyperos.slot=a" 1 1 > /dev/null && no "xroot= read as root="
plan "root=/dev/mmcblk0p2 notzyperos.slot=a" 1 1 > /dev/null && no "notzyperos.slot= read as the slot"
px2_slot_plan "$(line a 2)" 0 0 > /dev/null 2>&1 && no "a one-slot card was planned"
msg=$(px2_slot_plan "$(line a 2)" 0 0 2>&1 > /dev/null)
case "$msg" in *Installer*) ;; *) no "a one-slot card is not told to use the installer: $msg" ;; esac

# bootstate.txt: before the write the running slot boots, off trial, and the
# target holds nothing; after it the target boots on trial
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT
printf 'slot=a\ntrial=1\ntry=2\nfell=-\na=1.0.1\nb=1.0.0\n' > "$t/bs"
px2_bootstate_before "$t/bs" a b || no "before: failed"
[ "$(cat "$t/bs")" = "$(printf 'slot=a\ntrial=0\ntry=0\nfell=-\na=1.0.1\nb=-')" ] && ok ||
	no "before: $(tr '\n' ' ' < "$t/bs")"
px2_bootstate_after "$t/bs" b 1.0.2 || no "after: failed"
[ "$(cat "$t/bs")" = "$(printf 'slot=b\ntrial=1\ntry=0\nfell=-\na=1.0.1\nb=1.0.2')" ] && ok ||
	no "after: $(tr '\n' ' ' < "$t/bs")"
# A file from before a key was there gets it
printf 'slot=b\ntrial=0\ntry=0\n' > "$t/bs"
px2_bootstate_before "$t/bs" b a && px2_bootstate_after "$t/bs" a dev
[ "$(cat "$t/bs")" = "$(printf 'slot=a\ntrial=1\ntry=0\na=dev\nfell=-')" ] && ok ||
	no "missing keys: $(tr '\n' ' ' < "$t/bs")"
# A value that would edit more than its line is refused, the file as it was
printf 'slot=a\n' > "$t/bs"
px2_bootstate_set "$t/bs" a '1/0' 2> /dev/null && no "a slash in a value was written"
px2_bootstate_set "$t/bs" a '' 2> /dev/null && no "an empty value was written"
px2_bootstate_set "$t/bs" a '1.0
slot=b' 2> /dev/null && no "a new line in a value was written"
[ "$(cat "$t/bs")" = "slot=a" ] || no "a refused value changed the file"

# The device's script: it parses, and it writes partitions only through the
# plan ($PART), never one named by number
sed -n "/^	cat <<'REMOTE'/,/^REMOTE/p" px2-push.sh | sed '1d;$d' > "$t/remote"
[ -s "$t/remote" ] || no "the device's script was not found in px2-push.sh"
{ cat px2-slot.sh; echo 'RUNNING=a TARGET=b PART=mmcblk0p4 SYSTEM=dev DTB=x.dtb'; cat "$t/remote"; } > "$t/run.sh"
sh -n "$t/run.sh" || no "the device's script does not parse"
grep -n 'of=/dev/mmcblk0p\|/dev/mmcblk0p[0-9]* bs=' "$t/remote" && no "the device's script writes a partition by number"
grep -q 'of="/dev/$PART"' "$t/remote" || no "the device's script does not write \$PART"
sh -n px2-push.sh || no "px2-push.sh does not parse"

if [ "$fails" -gt 0 ]; then
	echo "px2-slot-check: $fails failed" >&2
	exit 1
fi
echo "px2-slot-check: the slot not running is written, never the running one; a card with one system, or a command line that does not add up, gets nothing; bootstate.txt edited as System Update does"
