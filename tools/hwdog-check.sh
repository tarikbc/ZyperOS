#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# plastron's hwdog (package/hwdog), against a plain file for /dev/watchdog
# and one for the kernel's log: from init's first line it is in the
# background at once, armed, and at a shutdown's TERM it stops the watchdog
# (the magic close, a "V") and says so; with no device it says so and ends;
# zytest=hang holds init and never opens the device; zytest=stall arms it and
# never pets it. The ioctls fail on a plain file, so the petting itself is
# the device's to show (docs/building.md). Linux only: linux/watchdog.h.
set -u
if [ "$(uname -s)" != Linux ]; then
	echo "skipped: hwdog builds on Linux (check-linux, CI)"
	exit 0
fi
T=$(mktemp -d "${TMPDIR:-/tmp}/hwdog-check.XXXXXX")
pids=
trap 'for p in $pids; do kill -9 $p 2> /dev/null; done; rm -rf "$T"' EXIT
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -O1 -DHWDOG_CHECK -o "$T/hwdog" \
	plastron/buildroot/package/hwdog/src/hwdog.c || exit 1
fails=0
ok() { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fails=$((fails + 1)); }
said() { grep -q "$1" "$T/kmsg" 2> /dev/null; }
# The daemon's pid, by the files it was given (its parent has gone)
daemon() {
	for p in /proc/[0-9]*; do
		tr '\0' '\n' < "$p/environ" 2> /dev/null | grep -q "^HWDOG_DEVICE=$T/$1\$" && echo "${p#/proc/}"
	done
}
wait_for() { i=0; while [ $i -lt 40 ] && ! said "$1"; do sleep 0.05; i=$((i + 1)); done; said "$1"; }

echo "from init's first line, with a device"
: > "$T/dev"; : > "$T/kmsg"
t0=$(date +%s)
HWDOG_DEVICE=$T/dev HWDOG_KMSG=$T/kmsg "$T/hwdog" -b
rc=$?
[ $rc = 0 ] && [ $(( $(date +%s) - t0 )) -le 1 ] && ok "init goes on at once" || bad "init waited, or $rc"
wait_for "hwdog: $T/dev armed" && ok "armed, said in the kernel's log" || bad "not armed: $(cat "$T/kmsg")"
p=$(daemon dev)
pids="$pids $p"
[ -n "$p" ] && ok "in the background ($p)" || bad "no daemon left"
[ -n "$p" ] && kill -TERM $p
wait_for "hwdog: stopped at the shutdown" && ok "a shutdown's TERM: stopped, said" || bad "no stop said"
[ "$(cat "$T/dev")" = V ] && ok "the magic close: a V" || bad "the device got \"$(cat "$T/dev")\", not V"
sleep 0.1
[ -n "$p" ] && [ ! -d /proc/$p ] && ok "and it ended" || bad "it is still there"

echo "no device"
: > "$T/kmsg"
HWDOG_DEVICE=$T/none HWDOG_KMSG=$T/kmsg "$T/hwdog" -b
wait_for "hwdog: no $T/none" && ok "said, nothing to pet" || bad "nothing said: $(cat "$T/kmsg")"
sleep 0.1
[ -z "$(daemon none)" ] && ok "and nothing left running" || bad "a daemon left with no device"

echo "zytest=hang: init held, the device never opened"
: > "$T/dev2"; : > "$T/kmsg"
zytest=hang HWDOG_DEVICE=$T/dev2 HWDOG_KMSG=$T/kmsg "$T/hwdog" -b &
h=$!
pids="$pids $h"
sleep 0.5
[ -d /proc/$h ] && ok "init still held after 0.5 s" || bad "init went on"
said "zytest=hang: init held here" && ok "said" || bad "not said"
[ ! -s "$T/dev2" ] && ! said "armed" && ok "the device never opened" || bad "the device was used"
kill -9 $h 2> /dev/null

echo "zytest=stall: armed, never petted"
: > "$T/dev3"; : > "$T/kmsg"
zytest=stall HWDOG_DEVICE=$T/dev3 HWDOG_KMSG=$T/kmsg "$T/hwdog" -b
wait_for "zytest=stall: never petted" && said "armed" && ok "armed and said never petted" ||
	bad "not said: $(cat "$T/kmsg")"
pids="$pids $(daemon dev3)"

[ "$fails" = 0 ] && echo "ok: hwdog arms the watchdog at once, stops it at a shutdown, and its two tests" && exit 0
exit 1
