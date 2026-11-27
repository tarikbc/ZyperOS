#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# plastron's /usr/sbin/crashlog against a stand-in pstore folder: a panic's
# dump becomes a crash-*.txt report with the kernel's last lines and is let
# go, a console that ends in the kernel's own restart says nothing, one that
# ends some other way (a reset Linux did not ask for) is a report too, the
# RCU stall panic is set at every start, and only the newest CRASH_KEEP
# reports stay; the launcher's own crash (--launcher RECORD) is a report the
# same way, once, and touches nothing of the kernel's; a start's marker on the
# card (ZYSTART) left by a start that lost its RAM before the home is a report
# too, W when the kernel's line has U-Boot's zyreset=chip. The boot partition
# is the device's, so not here. Runs with
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
		LASTBOOT=$T/lastboot ZYSTART=$T/card/.plastron/starting.txt CMDLINE=$T/cmdline \
		ZYSTART_SYNC_PID=$T/sync-pid sh "$CRASHLOG" > "$T/out" 2>&1
	# the marker's sync, in crashlog's background, done before the next step
	if [ -s "$T/sync-pid" ]; then
		while kill -0 "$(cat "$T/sync-pid")" 2> /dev/null; do sleep 0.05; done
		rm -f "$T/sync-pid"
	fi
}
fresh() {
	rm -rf "$T/pstore" "$T/logs" "$T/run-report" "$T/lastboot" "$T/card"
	mkdir -p "$T/pstore"
	echo 0 > "$T/rcu"
	echo 'root=/dev/mmcblk0p2 rootwait=20 zyperos.slot=a' > "$T/cmdline"
}
MARK=$T/card/.plastron/starting.txt
reports() { ls "$T/logs" 2> /dev/null | grep -c '^crash-.*\.txt$'; }

echo "nothing kept: nothing said"
fresh
run
[ "$(reports)" = 0 ] && ok "no report" || bad "a report with nothing in pstore"
[ "$(cat "$T/rcu")" = 1 ] && ok "the RCU stall panic is on" || bad "panic_on_rcu_stall is $(cat "$T/rcu")"
[ ! -f "$T/lastboot" ] && ok "no summary for the launcher" || bad "a summary with nothing in pstore"

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

# The summary the launcher says (ZyperOS's src/lastboot.h): reason, stage,
# the kernel's time and its last line, from what the kernel kept
summary_is() {   # what, reason, stage, kernel_time
	if grep -qx "reason=$2" "$T/lastboot" 2> /dev/null && grep -qx "stage=$3" "$T/lastboot" &&
	   grep -qx "kernel_time=$4" "$T/lastboot" && grep -q '^time=' "$T/lastboot" && grep -q '^last=' "$T/lastboot"; then
		ok "$1: $2 $3 at $4"
	else
		bad "$1: the summary is $(tr '\n' ' ' < "$T/lastboot" 2> /dev/null)"
	fi
}
echo "the summary for the launcher"
fresh
printf '[    0.430312] Waiting for root device /dev/mmcblk0p2...\r\n[    0.556257] mmcblk0: mmc0:aaaa SN128 119 GiB\r\n' > "$T/pstore/console-ramoops-0"
run
summary_is "stopped in the kernel before the card's partitions: the chip's watchdog" W K1 0.556257
grep -qx 'last=\[    0.556257\] mmcblk0: mmc0:aaaa SN128 119 GiB' "$T/lastboot" && ok "the last line, plain" ||
	bad "the last line: $(grep last= "$T/lastboot")"
fresh
printf '[    0.558306]  mmcblk0: p1 p2 p3 p4\n[    0.570133] VFS: Mounted root (squashfs filesystem) readonly\n' > "$T/pstore/console-ramoops-0"
run
summary_is "the card found, not init" W K2 0.570133
fresh
printf '[    0.558306]  mmcblk0: p1 p2 p3 p4\n[    0.572807] Run /sbin/init as init process\n[    0.701220] zyboot: stage=C1\n[    0.903400] zyboot: stage=C3\n[   14.200000] exfat: something\n' > "$T/pstore/console-ramoops-0"
run
summary_is "the card's check, a reset" W C3 14.200000
fresh
printf '[    0.720000] zyboot: stage=L1\n[    1.736000] zyperos: first frame at 1.736 s\n[  121.000000] zyperos: watchdog: the loop has not come round for 60 s: the screen off, a restart at 120 s\n[  181.000000] zyperos: watchdog: the loop has not come round for 120 s: an emergency sync, and a restart\n[  181.050000] sysrq: Emergency Sync\n[  181.100000] sysrq: Resetting\n' > "$T/pstore/console-ramoops-0"
run
summary_is "the launcher's watchdog after the home" W H 181.100000
fresh
printf '<3>[  243.1] INFO: task zyperos.elf:412 blocked for more than 120 seconds.\n<0>[  243.2] Kernel panic - not syncing: hung_task: blocked tasks\n' > "$T/pstore/dmesg-ramoops-0"
printf '[    0.720000] zyboot: stage=L1\n[  243.2] Kernel panic - not syncing: hung_task: blocked tasks\n' > "$T/pstore/console-ramoops-0"
run
summary_is "hung_task's panic" T L1 243.2
fresh
printf '<4>[   20.43] Waiting for root device /dev/mmcblk0p2...\n<0>[   20.44] VFS: Cannot open root device "mmcblk0p2"\n<0>[   20.45] Kernel panic - not syncing: VFS: Unable to mount root fs\n' > "$T/pstore/dmesg-ramoops-0"
run
summary_is "rootwait gave up" R K1 20.45
fresh
printf '<0>[ 3600.1] watchdog: BUG: soft lockup - CPU#2 stuck for 22s! [retroarch:812]\n<0>[ 3600.2] Kernel panic - not syncing: softlockup: hung tasks\n' > "$T/pstore/dmesg-ramoops-0"
run
summary_is "a lockup an hour in, its start's lines gone" P H 3600.2
fresh
printf '[    1.736000] zyperos: first frame at 1.736 s\n[  900.000000] rk817: battery 3%%\n' > "$T/pstore/console-ramoops-0"
run
summary_is "a reset after the home with nothing to say" X H 900.000000
fresh
printf '[    1.736000] zyperos: first frame at 1.736 s\n[  300.000000] zyperos: watchdog: the loop came round again after 64 s: the screen on\n[  900.000000] rk817: battery 3%%\n' > "$T/pstore/console-ramoops-0"
run
summary_is "the launcher's watchdog came back, then a reset" X H 900.000000
grep -q '^report=crash-.*\.txt$' "$T/lastboot" && ok "the report's name" || bad "no report= in the summary"

fresh
printf '<0>[  121.4] Kernel panic - not syncing: watchdog pretimeout event\n' > "$T/pstore/dmesg-ramoops-0"
printf '[    0.572807] Run /sbin/init as init process\n[  121.4] Kernel panic - not syncing: watchdog pretimeout event\n' > "$T/pstore/console-ramoops-0"
run
summary_is "the chip's watchdog's first stage, its pre-timeout panic" W C1 121.4

echo "the chip's watchdog, from U-Boot"
fresh
echo 'root=/dev/mmcblk0p2 zyperos.slot=a zyreset=chip' > "$T/cmdline"
printf '[    1.736000] zyperos: first frame at 1.736 s\n[  900.000000] rk817: battery 3%%\n' > "$T/pstore/console-ramoops-0"
run
summary_is "a reset after the home that U-Boot read as the chip's own" W H 900.000000
r=$(ls "$T/logs"/crash-*.txt 2> /dev/null | head -n 1)
grep -q "U-Boot read the reset as: chip" "$r" 2> /dev/null && ok "the report names the chip's reset" || bad "no chip's reset in the report"
fresh
echo 'root=/dev/mmcblk0p2 zyreset=wdt' > "$T/cmdline"
printf '[  10.0] zyboot: stage=L1\n[  30.0] reboot: Restarting system\n' > "$T/pstore/console-ramoops-0"
run
[ "$(reports)" = 0 ] && [ ! -f "$T/lastboot" ] && ok "a console that ends in a restart says nothing, zyreset or not" ||
	bad "a report after a restart asked for"

echo "the start's marker, for a reset that took the RAM"
fresh
run
grep -qx 'stage=C1' "$MARK" 2> /dev/null && grep -q '^start=' "$MARK" && ok "every start makes its marker, C1" ||
	bad "no marker made: $(cat "$MARK" 2> /dev/null)"
[ "$(reports)" = 0 ] && ok "and a start with no marker before it says nothing" || bad "a report with no marker"
printf 'stage=L1\n' >> "$MARK"
run
summary_is_nt() {   # what, reason, stage: a summary with no kernel_time
	if grep -qx "reason=$2" "$T/lastboot" 2> /dev/null && grep -qx "stage=$3" "$T/lastboot" &&
	   ! grep -q '^kernel_time=' "$T/lastboot" && grep -q '^time=' "$T/lastboot"; then
		ok "$1: $2 $3, no kernel time"
	else
		bad "$1: the summary is $(tr '\n' ' ' < "$T/lastboot" 2> /dev/null)"
	fi
}
summary_is_nt "a start that stopped at L1, nothing kept, no chip's reason" X L1
r=$(ls "$T/logs"/crash-*.txt 2> /dev/null | head -n 1)
grep -q "did not reach ZyperOS's home" "$r" 2> /dev/null && grep -q 'its last step L1' "$r" &&
	ok "the report says the start and its step" || bad "the report: $(cat "$r" 2> /dev/null)"
[ "$(grep -c '^stage=' "$MARK")" = 1 ] && grep -qx 'stage=C1' "$MARK" && ok "the marker is this start's now" ||
	bad "the marker: $(cat "$MARK")"
fresh
run
echo 'root=/dev/mmcblk0p2 zyreset=wdt' > "$T/cmdline"
printf 'stage=U2\nstage=L' >> "$MARK"
run
summary_is_nt "the chip's watchdog, a cut last line" W U2
fresh
run
rm -f "$MARK"
run
[ "$(reports)" = 0 ] && ok "a marker the home removed: nothing" || bad "a report after a start that reached the home"
fresh
run
printf '[  10.0] zyboot: stage=L1\n[  12.0] reboot: Power down\n' > "$T/pstore/console-ramoops-0"
run
[ "$(reports)" = 0 ] && ok "a marker with a console that ends in a power-off: the console says" ||
	bad "a report from the marker with the kernel's power-off kept"

[ "$fails" = 0 ] && echo "ok: crashlog keeps a panic's and a reset's report, once, and its summary for the launcher, and the launcher's own report" && exit 0
exit 1
