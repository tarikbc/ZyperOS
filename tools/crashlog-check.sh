#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# plastron's /usr/sbin/crashlog against a stand-in pstore folder: a panic's
# dump becomes a crash-*.txt report with the kernel's last lines and is let
# go, a console that ends in the kernel's own restart says nothing, one that
# ends some other way (a reset Linux did not ask for) is a report too, the
# RCU stall panic is set at every start, and only the newest CRASH_KEEP
# reports stay; the launcher's own crash (--launcher RECORD) is a report the
# same way, once, and touches nothing of the kernel's. The boot partition is
# the device's, so not here. Runs with
# the computer's sh; the device's is busybox 1.37's ash.
set -u
CRASHLOG=plastron/buildroot/board/px2/rootfs-overlay/usr/sbin/crashlog
T=$(mktemp -d "${TMPDIR:-/tmp}/crashlog-check.XXXXXX")
trap 'rm -rf "$T"' EXIT
# As root with the right to mount (a cloud VM, a privileged container),
# crashlog's own `mount -t pstore` put the computer's pstore over the
# stand-in folder: nothing could be written in it, and it stayed mounted.
# This mount refuses, as mount does for a user on CI.
mkdir "$T/bin"
printf '#!/bin/sh\nexit 32\n' > "$T/bin/mount"
chmod +x "$T/bin/mount"
PATH=$T/bin:$PATH
fails=0
ok() { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fails=$((fails + 1)); }

run() {
	PSTORE=$T/pstore RCU_PANIC=$T/rcu RUN_REPORT=$T/run-report CRASH_DIR=$T/logs CRASH_KEEP=3 \
		sh "$CRASHLOG" > "$T/out" 2>&1
}
fresh() {
	rm -rf "$T/pstore" "$T/logs" "$T/run-report"
	mkdir -p "$T/pstore"
	echo 0 > "$T/rcu"
}
reports() { ls "$T/logs" 2> /dev/null | grep -c '^crash-.*\.txt$'; }

echo "nothing kept: nothing said"
fresh
run
[ "$(reports)" = 0 ] && ok "no report" || bad "a report with nothing in pstore"
[ "$(cat "$T/rcu")" = 1 ] && ok "the RCU stall panic is on" || bad "panic_on_rcu_stall is $(cat "$T/rcu")"

echo "a panic"
fresh
printf '<0>[  61.2] watchdog: BUG: soft lockup - CPU#2 stuck for 22s! [zyperos.elf:412]\n<0>[  61.3] Kernel panic - not syncing: softlockup: hung tasks\n' > "$T/pstore/dmesg-ramoops-0"
printf '[  40.0] usb 1-1: new device\r\n[  61.3] Kernel panic - not syncing: softlockup: hung tasks\r\n' > "$T/pstore/console-ramoops-0"
run
[ "$(reports)" = 1 ] && ok "one report" || bad "$(reports) reports for one panic: $(cat "$T/out")"
r=$(ls "$T/logs"/crash-*.txt 2> /dev/null | head -n 1)
grep -q 'stopped with a panic' "$r" 2> /dev/null && grep -q 'soft lockup - CPU#2' "$r" &&
	ok "it says a panic, with the dump" || bad "the report lacks the panic or its dump"
grep -q 'new device' "$r" 2> /dev/null && ok "and the end of the console" || bad "no console in the report"
grep -q "$(printf '\r')" "$r" 2> /dev/null && bad "carriage returns left in the report" || ok "plain line ends"
[ ! -f "$T/pstore/dmesg-ramoops-0" ] && ok "the dump is let go" || bad "the dump is still in pstore"
[ -f "$T/run-report" ] && ok "the report stays for setuplog" || bad "no report left for setuplog"
# The start after, as the kernel leaves it: the dump gone, and its console
# that start's own, which ended in a restart asked for
printf '[  90.0] reboot: Restarting system\n' > "$T/pstore/console-ramoops-0"
run
[ "$(reports)" = 1 ] && ok "said once: the next start adds nothing" || bad "the next start said it again"
# Two reports in one second each keep their own file
printf 'Kernel panic\n' > "$T/pstore/dmesg-ramoops-0"
run
printf 'Kernel panic\n' > "$T/pstore/dmesg-ramoops-0"
run
[ "$(reports)" -ge 2 ] && ok "a second report in the same second is a file of its own" ||
	bad "$(reports) reports after two more panics"

echo "a console that ends in the kernel's own restart"
fresh
printf '[ 200.1] zyperos: restart for System Update\n[ 201.0] reboot: Restarting system\n' > "$T/pstore/console-ramoops-0"
run
[ "$(reports)" = 0 ] && ok "no report after a restart asked for" || bad "a report after a normal restart"
printf '[ 300.0] reboot: Power down\n' > "$T/pstore/console-ramoops-0"
run
[ "$(reports)" = 0 ] && ok "nor after a power-off" || bad "a report after a power-off"

echo "a reset without the kernel's restart"
fresh
printf '[  52.7] mmc0: card status 0x900\n[  53.0] rockchip-thermal: thermal shutdown\n' > "$T/pstore/console-ramoops-0"
run
r=$(ls "$T/logs"/crash-*.txt 2> /dev/null | head -n 1)
grep -q 'reset before this start' "$r" 2> /dev/null && grep -q 'thermal shutdown' "$r" &&
	ok "a report of the reset, with the console" || bad "no report of a reset: $(cat "$T/out")"

echo "the newest three kept"
fresh
mkdir -p "$T/logs"
for d in 20261101-000000 20261102-000000 20261103-000000 20261104-000000; do
	echo old > "$T/logs/crash-$d.txt"
done
echo "not a report" > "$T/logs/zyperos.log"
printf 'Kernel panic\n' > "$T/pstore/dmesg-ramoops-1"
run
[ "$(reports)" = 3 ] && ok "three reports left" || bad "$(reports) reports left, not 3"
[ ! -f "$T/logs/crash-20261101-000000.txt" ] && [ ! -f "$T/logs/crash-20261102-000000.txt" ] &&
	[ -f "$T/logs/crash-20261104-000000.txt" ] && ok "the oldest went" || bad "the wrong ones went: $(ls "$T/logs")"
[ -f "$T/logs/zyperos.log" ] && ok "other files left alone" || bad "zyperos.log went"

echo "the launcher's own crash (crashlog --launcher, src/crashrec.h)"
fresh
printf 'crash: SIGSEGV (code 1) at 0x10 in thread zyperos, pid 812, ZyperOS 1.1.1\ncrash: backtrace, each frame the binary and its offset (addr2line -e with this build):\n/mnt/SDCARD/ZyperOS/zyperos(+0x8a1c4)[0x5585e8a1c4]\r\n/lib/libc.so.6(+0x2773c)[0x7f9a12773c]\n' > "$T/record"
launcher() {
	PSTORE=$T/pstore RCU_PANIC=$T/rcu RUN_REPORT=$T/run-report CRASH_DIR=$T/logs CRASH_KEEP=3 \
		sh "$CRASHLOG" --launcher "$T/record" > "$T/out" 2>&1
}
launcher
[ "$(reports)" = 1 ] && ok "one report" || bad "$(reports) reports for the launcher's crash: $(cat "$T/out")"
r=$(ls "$T/logs"/crash-*.txt 2> /dev/null | head -n 1)
grep -q "launcher stopped with SIGSEGV" "$r" 2> /dev/null && ok "it says the launcher stopped, and with which signal" ||
	bad "the report does not say the launcher's signal"
grep -q 'at 0x10 in thread zyperos' "$r" 2> /dev/null && grep -q 'zyperos(+0x8a1c4)' "$r" &&
	ok "with the record: the address, the thread and the backtrace" || bad "the record is not in the report"
grep -q "$(printf '\r')" "$r" 2> /dev/null && bad "carriage returns left in the report" || ok "plain line ends"
[ ! -f "$T/record" ] && ok "the record is let go" || bad "the record is still there"
[ ! -f "$T/run-report" ] && ok "setuplog's report left alone" || bad "a launcher's crash became setuplog's report"
[ "$(cat "$T/rcu")" = 0 ] && ok "nothing of the kernel's done" || bad "the launcher's crash set the RCU panic"
launcher
[ "$(reports)" = 1 ] && ok "said once: no record, nothing more" || bad "a report with no record"
: > "$T/record"
launcher
[ "$(reports)" = 1 ] && ok "an empty record says nothing" || bad "a report of an empty record"
printf 'Kernel panic\n' > "$T/pstore/dmesg-ramoops-0"
printf 'crash: SIGBUS (code 2) at 0x7f00 in thread zyperos-after\n' > "$T/record"
launcher
[ "$(reports)" = 2 ] && [ -f "$T/pstore/dmesg-ramoops-0" ] &&
	ok "the kernel's dumps are left for the boot's own run" || bad "the launcher mode took the kernel's dump"

[ "$fails" = 0 ] && echo "ok: crashlog keeps a panic's, a reset's and the launcher's report, once" && exit 0
exit 1
