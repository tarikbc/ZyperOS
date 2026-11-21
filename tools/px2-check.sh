#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# make check-px2: tools/px2, the computer's side of a debugging session, with
# a stand-in for ssh (PX2_SSH) that answers the card's size and runs the rest
# here, against stand-ins for the device's tools that print what they were
# given. No device, and never /tmp/px2-lock: the lock is a scratch folder
# (PX2_LOCK).
#
# Arguments: a command it does not know, or one given the wrong arguments, is
# refused before the lock is taken or the device is reached. The card: the
# test card and a VM's go ahead, the owner's 128 GB card and any other only
# with --real-card. The lock: taken with an owner file naming the command,
# let go after a command that worked, one that failed and one stopped by
# Ctrl-C or a TERM; a lock someone else holds is a refusal naming them, left
# as it is, or a wait with --wait. And each argument reaches the device's tool
# as the one word it was here, spaces and quotes and all.
set -u

D=$PWD/build-native/px2-check
PX2=$PWD/tools/px2
rm -rf "$D"
mkdir -p "$D/bin" || exit 1

# ssh as tools/px2 calls it: options, then root@HOST, then the command
cat > "$D/ssh" <<'EOF'
#!/bin/sh
for a in "$@"; do last=$a; done
echo "ssh" >> "$FAKE_DIR/calls"
[ -n "${PX2_LOCK:-}" ] && [ -f "$PX2_LOCK/owner" ] && cp "$PX2_LOCK/owner" "$FAKE_DIR/owner-seen"
case $last in
*/sys/block/mmcblk0/size*) echo "$FAKE_CARD"; exit 0 ;;
esac
PATH=$FAKE_DIR/bin:$PATH exec sh -c "$last"
EOF
for t in px2-input px2-snap px2-perf px2-open px2-shot; do
	printf '#!/bin/sh\necho %s\nfor a in "$@"; do printf "[%%s]\\n" "$a"; done\n[ -n "${FAKE_SLEEP:-}" ] && sleep "$FAKE_SLEEP"\nexit "${FAKE_RC:-0}"\n' "$t" > "$D/bin/$t"
done
chmod +x "$D/ssh" "$D/bin/"*

export FAKE_DIR="$D" FAKE_CARD=62333952 PX2_SSH="$D/ssh" PX2_LOCK="$D/lock" PX2_HOST=192.0.2.1
px2() { "$PX2" "$@"; }

fails=0
ok() { printf '  ok   %s\n' "$1"; }
bad() { printf '  FAIL %s\n' "$1"; fails=$((fails + 1)); }
check() { if eval "$2"; then ok "$1"; else bad "$1"; fi; }
calls() { [ -f "$D/calls" ] && wc -l < "$D/calls" | tr -d ' ' || echo 0; }

# Arguments, refused before anything
rm -f "$D/calls"
for args in "" "frob" "open" "open a b" "open --fresh" "quit now" "status x" "press" \
	"perf abc" "perf 5 x" "snap" "snap frob" "snap save" "snap restore" "snap list x" \
	"log -x" "--wait" "--wait x status" "--bogus status"; do
	# shellcheck disable=SC2086
	px2 $args > /dev/null 2>&1
	rc=$?
	[ $rc -eq 2 ] || bad "'tools/px2 $args' exits 2, not $rc"
done
check "nineteen wrong uses refused with 2, before the device is reached" '[ "$(calls)" = 0 ]'
check "and no lock taken for them" '[ ! -e "$D/lock" ]'

# The card
out=$(px2 status 2>&1)
check "the test card goes ahead" 'printf "%s" "$out" | grep -qx "px2-open" && printf "%s" "$out" | grep -qx "\[--status\]"'
check "the lock is let go after" '[ ! -e "$D/lock" ]'
check "and was held with an owner file naming the command and this process" \
	'grep -qx "command=tools/px2 status " "$D/owner-seen" && grep -q "^pid=[0-9]" "$D/owner-seen"'
out=$(FAKE_CARD=249737216 "$PX2" status 2>&1); rc=$?
check "the owner's 128 GB card is refused" '[ $rc -eq 1 ] && printf "%s" "$out" | grep -q "128 GB" && ! printf "%s" "$out" | grep -q "^px2-open"'
check "and the lock let go" '[ ! -e "$D/lock" ]'
out=$(FAKE_CARD=249737216 "$PX2" --real-card status 2>&1); rc=$?
check "but used with --real-card" '[ $rc -eq 0 ] && printf "%s" "$out" | grep -qx "px2-open"'
out=$(FAKE_CARD=124735488 "$PX2" status 2>&1); rc=$?
check "any other card is refused too" '[ $rc -eq 1 ] && printf "%s" "$out" | grep -q "not the test card"'
out=$(FAKE_CARD="vm 8388608" "$PX2" status 2>&1); rc=$?
check "a VM's card goes ahead" '[ $rc -eq 0 ]'
out=$(FAKE_CARD=none "$PX2" status 2>&1); rc=$?
check "no card at all is refused" '[ $rc -eq 1 ]'

# Each argument one word on the device
out=$(px2 press "a" "start:600" "select+start" 2>&1)
check "press: each button as given" '[ "$out" = "$(printf "px2-input\n[a]\n[start:600]\n[select+start]")" ]'
out=$(px2 snap save "/mnt/SDCARD/Roms/NES/Tom's Game (USA) \$HOME.nes" "my-name" 2>&1)
check "snap: a ROM with spaces, a quote and a dollar arrives whole" \
	'[ "$out" = "$(printf "px2-snap\n[save]\n[/mnt/SDCARD/Roms/NES/Tom'"'"'s Game (USA) \$HOME.nes]\n[my-name]")" ]'
out=$(px2 open "Roms/N64/A \"Quoted\" Game.z64" --fresh 2>&1)
check "open: the ROM whole, and --fresh" \
	'[ "$out" = "$(printf "px2-open\n[Roms/N64/A \"Quoted\" Game.z64]\n[--fresh]")" ]'
out=$(px2 quit 2>&1)
check "quit is px2-open --quit" '[ "$out" = "$(printf "px2-open\n[--quit]")" ]'
out=$(px2 perf 3 --json 2>&1)
check "perf: its seconds and --json" '[ "$out" = "$(printf "px2-perf\n[3]\n[--json]")" ]'

# A tool that fails: its status, and the lock let go
FAKE_RC=3 "$PX2" press a > /dev/null 2>&1; rc=$?
check "a device command's failure is the exit status" '[ $rc -eq 3 ]'
check "and the lock is let go" '[ ! -e "$D/lock" ]'

# Someone else's lock
mkdir "$D/lock" && printf 'pid=1\ncommand=tools/px2 perf 60\n' > "$D/lock/owner"
rm -f "$D/calls"
out=$(px2 status 2>&1); rc=$?
check "a lock held is a refusal (75) naming its owner" \
	'[ $rc -eq 75 ] && printf "%s" "$out" | grep -q "command=tools/px2 perf 60"'
check "the device is not reached" '[ "$(calls)" = 0 ]'
check "and the other's lock is left as it was" '[ "$(cat "$D/lock/owner")" = "$(printf "pid=1\ncommand=tools/px2 perf 60")" ]'
( sleep 2; rm -rf "$D/lock" ) &
out=$(px2 --wait 10 status 2>&1); rc=$?
wait
check "--wait waits for it to be let go, then goes ahead" '[ $rc -eq 0 ] && printf "%s" "$out" | grep -qx "px2-open" && [ ! -e "$D/lock" ]'
mkdir "$D/lock" && printf 'pid=1\n' > "$D/lock/owner"
px2 --wait 1 status > /dev/null 2>&1; rc=$?
check "and gives up after its seconds" '[ $rc -eq 75 ] && [ -d "$D/lock" ]'
rm -rf "$D/lock"

# Stopped halfway: TERM, and Ctrl-C (SIGINT), which a shell's background job
# would ignore, so sent from Python to a process it started
FAKE_SLEEP=5 "$PX2" perf 5 > /dev/null 2>&1 &
p=$!
i=0
while [ ! -d "$D/lock" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
sleep 0.5
kill -TERM $p
wait $p; rc=$?
check "TERM halfway: the lock let go, 143" '[ $rc -eq 143 ] && [ ! -e "$D/lock" ]'
rc=$(python3 - "$PX2" "$D/lock" <<'EOF'
import os, signal, subprocess, sys, time
env = dict(os.environ, FAKE_SLEEP="5")
p = subprocess.Popen([sys.argv[1], "perf", "5"], env=env, stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL, start_new_session=True)
for _ in range(50):
    if os.path.isdir(sys.argv[2]): break
    time.sleep(0.1)
time.sleep(0.5)
os.killpg(p.pid, signal.SIGINT)     # as a terminal's Ctrl-C: the whole group
print(p.wait(10))
EOF
)
check "Ctrl-C halfway: the lock let go, 130 ($rc)" '[ "$rc" = 130 ] && [ ! -e "$D/lock" ]'

# The default lock is the Pixel 2's own for the Pixel 2, on the cable or over
# Wi-Fi (a device on port 22), and a VM's own for anything on this computer
lock_for() {
	# The lock tools/px2 picks, run as far as its choice and no further
	PX2_LOCK= PX2_HOST=$1 PX2_PORT=$2 sh -c '
		HOST=$PX2_HOST PORT=$PX2_PORT
		eval "$(sed -n "/^if \[ -n \"\${PX2_LOCK:-}\" \]; then LOCK=/,/^fi$/p" "$0")"
		echo "$LOCK"' "$PX2"
}
check "the Pixel 2 on the cable takes /tmp/px2-lock ($(lock_for 10.42.0.1 22))" \
	'[ "$(lock_for 10.42.0.1 22)" = /tmp/px2-lock ]'
check "and over Wi-Fi, by address or name ($(lock_for 192.168.1.23 22), $(lock_for zyperos.local 22))" \
	'[ "$(lock_for 192.168.1.23 22)" = /tmp/px2-lock ] && [ "$(lock_for zyperos.local 22)" = /tmp/px2-lock ]'
check "a VM takes its own ($(lock_for 127.0.0.1 2222))" \
	'[ "$(lock_for 127.0.0.1 2222)" = /tmp/px2-lock-127.0.0.1-2222 ] &&
	 [ "$(lock_for 127.0.0.1 22)" = /tmp/px2-lock-127.0.0.1-22 ] &&
	 [ "$(lock_for 192.168.1.23 2222)" = /tmp/px2-lock-192.168.1.23-2222 ]'

# deploy and restore (tools/px2-deploy.sh under the lock), against a device
# that is folders here: the card (PX2_CARD), /tmp's places, the image's own
# ZyperOS (PX2_SEED) and system files (PX2_ROOT), /proc (PX2_PROC: the mount
# namespaces, each process's exe and comm, the boot's id), and stand-ins for
# the commands the device has: scp a copy, pidof the processes written down
# here, kill (PX2_KILL) one that ends them as launch.sh and init would start
# them again, reboot a new boot, mount and umount a list of binds, and the
# dynamic loader (PX2_RTLD) answering as glibc's does for ldd -r
T=$(printf '\t')
cat > "$D/scp" <<'EOF'
#!/bin/sh
# scp as tools/px2-deploy.sh calls it: options, the folder, root@HOST:PATH;
# FAKE_DAMAGE spoils one file on the way
for a in "$@"; do from=$to; to=$a; done
cp -R "$from" "${to#*:}"
[ -z "${FAKE_DAMAGE:-}" ] || echo spoiled >> "${to#*:}/card/$FAKE_DAMAGE"
echo "scp $from" >> "$FAKE_DIR/calls-scp"
EOF
cat > "$D/bin/pidof" <<'EOF'
#!/bin/sh
out=
for n in "$@"; do
	[ -f "$FAKE_DIR/pids/$n" ] && out="$out $(cat "$FAKE_DIR/pids/$n")"
done
[ -n "$out" ] || exit 1
echo $out
EOF
cat > "$D/bin/fakekill" <<'EOF'
#!/bin/sh
# kill -SIG PID: the process ends; the launcher's end with the restart file
# there starts launch.sh's next pass (the resident Diatom if it is gone, then
# the launcher), gauge's starts init's next one
sig=$1 pid=$2
P=$FAKE_DIR/proc
[ -d "$P/$pid" ] || exit 1
name=$(cat "$P/$pid/comm")
echo "kill $sig $name" >> "$FAKE_DIR/kills"
rm -rf "${P:?}/$pid"
rm -f "$FAKE_DIR/pids/$name"
case $name in
zyperos.elf)
	if [ -f "$FAKE_DIR/flags/zyperos_restart" ]; then
		rm -f "$FAKE_DIR/flags/zyperos_restart"
		echo "launch.sh: restart" >> "$FAKE_DIR/kills"
		p=$(cat "$FAKE_DIR/flags/diatom.pid" 2> /dev/null)
		[ -n "$p" ] && [ -d "$P/$p" ] || "$FAKE_DIR/start" diatom "$FAKE_DIR/card/ZyperOS/diatom"
		"$FAKE_DIR/start" zyperos.elf "$FAKE_DIR/card/ZyperOS/zyperos.elf"
	fi ;;
gauge)
	f=$FAKE_DIR/root/usr/sbin/gauge
	b=$(awk -F'\t' -v d="$f" '$2 == d { s = $1 } END { print s }' "$FAKE_DIR/binds" 2> /dev/null)
	"$FAKE_DIR/start" gauge "${b:-$f}" ;;
esac
EOF
cat > "$D/start" <<'EOF'
#!/bin/sh
# A process named $1 running the file $2
n=$(($(cat "$FAKE_DIR/lastpid" 2> /dev/null || echo 9000) + 1))
echo $n > "$FAKE_DIR/lastpid"
mkdir -p "$FAKE_DIR/proc/$n"
cp "$2" "$FAKE_DIR/proc/$n/exe"
echo "$1" > "$FAKE_DIR/proc/$n/comm"
echo $n > "$FAKE_DIR/pids/$1"
[ "$1" = diatom ] && echo $n > "$FAKE_DIR/flags/diatom.pid"
exit 0
EOF
cat > "$D/bin/reboot" <<'EOF'
#!/bin/sh
# A new boot: /tmp and the binds gone, a new boot id, ZyperOS started again
echo reboot >> "$FAKE_DIR/kills"
rm -rf "$FAKE_DIR/systmp" "$FAKE_DIR/devtmp" "$FAKE_DIR/binds" "$FAKE_DIR/pids" "$FAKE_DIR/flags"
mkdir -p "$FAKE_DIR/pids" "$FAKE_DIR/flags"
for p in "$FAKE_DIR"/proc/[0-9]*; do rm -rf "$p"; done
echo "boot-$(date +%s)-$$" > "$FAKE_DIR/proc/sys/kernel/random/boot_id"
"$FAKE_DIR/start" diatom "$FAKE_DIR/card/ZyperOS/diatom"
"$FAKE_DIR/start" zyperos.elf "$FAKE_DIR/card/ZyperOS/zyperos.elf"
"$FAKE_DIR/start" gauge "$FAKE_DIR/root/usr/sbin/gauge"
EOF
cat > "$D/bin/mount" <<'EOF'
#!/bin/sh
echo "mount $* ns=${FAKE_NS_SEEN:-ssh}" >> "$FAKE_DIR/mount-calls"
[ "$1" = -o ] && [ "$2" = bind ] || exit 1
printf '%s\t%s\n' "$3" "$4" >> "$FAKE_DIR/binds"
EOF
cat > "$D/bin/umount" <<'EOF'
#!/bin/sh
echo "umount $* ns=${FAKE_NS_SEEN:-ssh}" >> "$FAKE_DIR/mount-calls"
awk -F'\t' -v d="$1" '$2 != d' "$FAKE_DIR/binds" > "$FAKE_DIR/binds.new" && mv "$FAKE_DIR/binds.new" "$FAKE_DIR/binds"
EOF
cat > "$D/bin/ld-fake" <<'EOF'
#!/bin/sh
# glibc's loader asked as ldd -r asks it (by the names PX2_LDENV gives the
# three variables here); a program it runs says so
if [ "${FAKE_TRACE_LOADED_OBJECTS:-}" != 1 ] || [ "${FAKE_BIND_NOW:-}" != yes ] || [ "${FAKE_WARN:-}" != yes ]; then
	echo "$1" >> "$FAKE_DIR/ran"
	exit 0
fi
echo "$1" >> "$FAKE_DIR/ld-asked"
printf '\tlinux-vdso.so.1 (0x0000ffff00000000)\n\tlibc.so.6 => /lib/libc.so.6 (0x0000ffff00010000)\n'
grep -q NEEDS-MISSING "$1" && printf '\tlibmissing.so.9 => not found\n'
grep -q NEEDS-NEWER "$1" && printf 'undefined symbol: SDL_Future\t(%s)\n' "$1"
exit 0
EOF
# The helper a release image's own namespace needs, as this computer built it
cat > "$D/initns-local" <<'EOF'
#!/bin/sh
echo "initns $*" >> "$FAKE_DIR/initns-calls"
FAKE_NS_SEEN=init exec "$@"
EOF
chmod +x "$D/scp" "$D/start" "$D/bin/"* "$D/initns-local"

# A file that starts as an ELF file does: the loader is asked about it
elf() { printf '\177ELF %s\n' "$2" > "$1"; }
# The image's own ZyperOS and its index, as plastron's post-build writes it
index() {
	(cd "$1" && find . -type f ! -path ./.files | sed 's|^\./||' | LC_ALL=C sort |
	 while IFS= read -r f; do printf '%s %s %s\n' "$(sha256sum "$f" | cut -c1-64)" "$(wc -c < "$f" | tr -d ' ')" "$f"; done) > "$1/.files"
}
# The device as it starts: the image's ZyperOS on the card, the launcher,
# the resident Diatom and gauge running, SSH in init's namespace
fresh() {
	rm -rf "$D/card" "$D/seed" "$D/root" "$D/proc" "$D/pids" "$D/flags" "$D/systmp" "$D/devtmp" \
		"$D/binds" "$D/kills" "$D/mount-calls" "$D/initns-calls" "$D/ld-asked" "$D/ran" "$D/calls-scp" "$D/lastpid"
	S=$D/seed/ZyperOS
	mkdir -p "$S/cores" "$S/pico8" "$D/root/usr/lib/ladspa" "$D/root/usr/sbin" "$D/root/etc" \
		"$D/proc/1/ns" "$D/proc/self/ns" "$D/proc/sys/kernel/random" "$D/pids" "$D/flags"
	elf "$S/zyperos.elf" "slot launcher"
	elf "$S/diatom" "slot diatom"
	elf "$S/mp4d" "slot mp4d"
	elf "$S/pico8/p8fx.so" "slot p8fx"
	echo "slot launch.sh" > "$S/launch.sh"
	echo "slot eq-port.sh" > "$S/eq-port.sh"
	echo "a core" > "$S/cores/a.so"
	echo "1.1.0-rc.9" > "$S/VERSION"
	index "$S"
	mkdir -p "$D/card"
	cp -R "$S" "$D/card/ZyperOS"
	elf "$D/root/usr/lib/ladspa/zyperos_eq.so" "image eq"
	elf "$D/root/usr/sbin/gauge" "image gauge"
	echo "image asound" > "$D/root/etc/asound.conf"
	ln -s 'mnt:[4026531840]' "$D/proc/1/ns/mnt"
	ln -s 'mnt:[4026531840]' "$D/proc/self/ns/mnt"
	echo boot-1 > "$D/proc/sys/kernel/random/boot_id"
	"$D/start" diatom "$D/card/ZyperOS/diatom"
	"$D/start" zyperos.elf "$D/card/ZyperOS/zyperos.elf"
	"$D/start" gauge "$D/root/usr/sbin/gauge"
	rm -rf "$D/before"
	cp -R "$D/card" "$D/before"
}
# Developer Mode's SSH: a mount namespace of its own
own_ns() { rm -f "$D/proc/self/ns/mnt"; ln -s 'mnt:[4026532201]' "$D/proc/self/ns/mnt"; }
# The build: programs and scripts, and the system's files beside them
B=$D/build
build() {
	rm -rf "$B"
	P=$B/programs/ZyperOS
	mkdir -p "$P/pico8" "$B/system/usr/lib/ladspa" "$B/system/usr/sbin" "$B/system/etc"
	elf "$P/zyperos.elf" "slot launcher"
	elf "$P/diatom" "slot diatom"
	elf "$P/mp4d" "slot mp4d"
	elf "$P/pico8/p8fx.so" "slot p8fx"
	echo "slot launch.sh" > "$P/launch.sh"
	echo "slot eq-port.sh" > "$P/eq-port.sh"
	chmod +x "$P/zyperos.elf" "$P/diatom" "$P/mp4d" "$P/launch.sh"
	elf "$B/system/usr/lib/ladspa/zyperos_eq.so" "new eq"
	elf "$B/system/usr/sbin/gauge" "new gauge"
	echo "new asound" > "$B/system/etc/asound.conf"
	printf 'zyperos=e298086b-dirty\nsdk=plastron-sdk-px2-3c30193b7a0e\nsdk.id=3c30193b7a0e\nsdk.plastron=abc123\n' > "$B/programs/BUILT"
}
dep() {
	PX2_SCP="$D/scp" PX2_CARD="$D/card" PX2_DEVICE_TMP="$D/devtmp" PX2_DEVICE_SYSTMP="$D/systmp" \
	PX2_DEVICE_FLAGS="$D/flags" PX2_SEED="$D/seed" PX2_PROC="$D/proc" PX2_ROOT="$D/root" \
	PX2_RTLD="$D/bin/ld-fake" PX2_LDENV="FAKE_TRACE_LOADED_OBJECTS=1 FAKE_WARN=yes FAKE_BIND_NOW=yes" \
	PX2_KILL="$D/bin/fakekill" PX2_SYSTEM_DIR="$B/system" \
	PX2_INITNS="$D/initns-local" PX2_DEPLOY_WAIT=6 PX2_DEPLOY_REBOOT_PAUSE=0 "$PX2" "$@"
}
running() { cat "$D/proc/$(cat "$D/pids/$1")/exe" 2> /dev/null; }
said() { printf '%s\n' "$out" | grep -qF -- "$1"; }
cardsame() { diff -r "$D/before" "$D/card" > /dev/null 2>&1; }

check "the device's loader is asked as ldd -r asks it" \
	'grep -qx "LDENV=\${PX2_LDENV:-LD_TRACE_LOADED_OBJECTS=1 LD_WARN=yes LD_BIND_NOW=yes}" tools/px2-deploy.sh'

# A deploy of the programs: only what differs, the card's own kept, the
# launcher restarted the way it restarts itself and seen running the new one
fresh; build
elf "$P/zyperos.elf" "new launcher"
elf "$P/zyperos-update" "new updater"
out=$(dep deploy "$P" 2>&1); rc=$?
check "deploy: what changed is sent, what did not stays ($rc)" \
	'[ $rc -eq 0 ] && grep -q "new launcher" "$D/card/ZyperOS/zyperos.elf" && grep -q "new updater" "$D/card/ZyperOS/zyperos-update" &&
	 said "  send zyperos.elf (" && said "  send zyperos-update (" && ! said "send diatom" && ! said "send launch.sh" && said "(5 more the card has already)"'
check "it says what it was built from and against" 'said "e298086b-dirty against plastron-sdk-px2-3c30193b7a0e (plastron abc123)"'
check "and what the card and its system have" 'said "card has ZyperOS 1.1.0-rc.9, its system 1.1.0-rc.9"'
check "in one copy, with the programs asked about by the loader as ldd -r asks, never run" \
	'[ "$(grep -c . "$D/calls-scp")" = 1 ] && grep -q "card/zyperos.elf$" "$D/ld-asked" && grep -q "card/zyperos-update$" "$D/ld-asked" && [ ! -e "$D/ran" ]'
check "no file is left under its new name, nor the copy in /tmp" \
	'[ -z "$(find "$D/card" -name "*.px2-new")" ] && [ ! -e "$D/devtmp" ]'
check "the card's own launcher is kept with the system it came with, the new updater named as added" \
	'grep -q "slot launcher" "$D/card/.px2-deploy/orig/zyperos.elf" && [ "$(cat "$D/card/.px2-deploy/added")" = zyperos-update ] &&
	 [ "$(cat "$D/card/.px2-deploy/system")" = "$(sha256sum "$D/seed/ZyperOS/.files" | cut -c1-12)" ]'
check "the launcher restarted as the charging screen restarts it, Diatom left running" \
	'[ "$(cat "$D/kills")" = "$(printf "kill -TERM zyperos.elf\nlaunch.sh: restart")" ] && said "restarting ZyperOS"'
check "and seen running the new build" 'grep -q "new launcher" "$(echo "$D/proc/$(cat "$D/pids/zyperos.elf")/exe")" && said "zyperos.elf now runs that build"'
check "System Update's program needs no restart, and it says so" 'said "read at their next use" && said "zyperos-update"'
check "the lock let go after a deploy" '[ ! -e "$D/lock" ]'
out=$(dep deploy "$P" 2>&1); rc=$?
check "the same build again: nothing sent, nothing restarted" \
	'[ $rc -eq 0 ] && said "nothing sent, nothing restarted" && [ "$(grep -c . "$D/calls-scp")" = 1 ]'
elf "$P/zyperos.elf" "newer launcher"
dep deploy "$P" > /dev/null 2>&1
check "a second deploy keeps the first one's kept file" \
	'grep -q "newer launcher" "$D/card/ZyperOS/zyperos.elf" && grep -q "slot launcher" "$D/card/.px2-deploy/orig/zyperos.elf" &&
	 [ "$(cat "$D/card/.px2-deploy/added")" = zyperos-update ]'
out=$(dep restore --status 2>&1)
check "restore --status says what a restore puts back" \
	'said "  replaced zyperos.elf" && said "  added zyperos-update" && said "on system $(sha256sum "$D/seed/ZyperOS/.files" | cut -c1-12) (this one is"'
rm -f "$D/kills"
out=$(dep restore 2>&1); rc=$?
check "restore: the card byte for byte as before the first deploy ($rc)" '[ $rc -eq 0 ] && cardsame'
check "each file said to be the system's own copy, or removed" \
	'said "restored zyperos.elf (the system'"'"'s own)" && said "restored zyperos-update (removed, as before the deploy)"'
check "the launcher restarted, running the slot's own again" \
	'grep -q "kill -TERM zyperos.elf" "$D/kills" && grep -q "slot launcher" "$D/proc/$(cat "$D/pids/zyperos.elf")/exe"'
dep restore > /dev/null 2>&1; rc=$?
check "a restore with nothing kept is a refusal (3)" '[ $rc -eq 3 ]'

# The loader's no: a library or a symbol the system does not have
fresh; build
printf '\177ELF new launcher NEEDS-MISSING\n' > "$P/zyperos.elf"
printf '\177ELF new mp4d NEEDS-NEWER\n' > "$P/mp4d"
out=$(dep deploy "$P" 2>&1); rc=$?
check "a program built against what the system lacks is refused (6), naming it" \
	'[ $rc -eq 6 ] && said "zyperos.elf" && said "libmissing.so.9 => not found" && said "undefined symbol: SDL_Future" && said "nothing was changed"'
check "and the SDK it was built against, beside the system the Pixel runs" \
	'said "built against plastron-sdk-px2-3c30193b7a0e (plastron abc123), and the Pixel runs a system with ZyperOS 1.1.0-rc.9"'
check "and nothing changed: the card, the kept files, the processes" \
	'cardsame && [ ! -e "$D/card/.px2-deploy" ] && [ ! -e "$D/kills" ] && [ ! -e "$D/ran" ] && [ ! -e "$D/devtmp" ]'

# A file spoiled on the way
fresh; build
elf "$P/zyperos.elf" "new launcher"
out=$(FAKE_DAMAGE=zyperos.elf dep deploy "$P" 2>&1); rc=$?
check "a file damaged on the way is refused (5) and nothing changed" '[ $rc -eq 5 ] && said "arrived damaged" && cardsame && [ ! -e "$D/kills" ]'

# Busy: a game open, a port, Flash Drive
fresh; build
elf "$P/zyperos.elf" "new launcher"
printf 'pid=%s\nrom=/mnt/SDCARD/Roms/NES/Contra.nes\n' "$(cat "$D/pids/diatom")" > "$D/flags/diatom-now"
out=$(dep deploy "$P" 2>&1); rc=$?
check "a game open: refused before anything is sent, naming it" \
	'[ $rc -eq 1 ] && said "a game is open (/mnt/SDCARD/Roms/NES/Contra.nes)" && cardsame && [ ! -e "$D/calls-scp" ]'
out=$(dep deploy --no-restart "$P" 2>&1); rc=$?
check "--no-restart puts the files and restarts nothing" \
	'[ $rc -eq 0 ] && grep -q "new launcher" "$D/card/ZyperOS/zyperos.elf" && [ ! -e "$D/kills" ] && said "run once these start again: ZyperOS"'
fresh; build
elf "$P/zyperos.elf" "new launcher"
echo 9999 > "$D/pids/usbdrive"
out=$(dep deploy --no-restart "$P" 2>&1); rc=$?
check "the card lent to a computer (Flash Drive): never, --no-restart or not" '[ $rc -eq 1 ] && said "Flash Drive" && cardsame'
rm -f "$D/pids/usbdrive"
echo 9998 > "$D/pids/portkeys"
out=$(dep deploy "$P" 2>&1); rc=$?
check "a port running: refused" '[ $rc -eq 1 ] && said "a port is running" && cardsame'
rm -f "$D/pids/portkeys"

# Diatom, a card script, a script read at the next port
fresh; build
elf "$P/diatom" "new diatom"
out=$(dep deploy "$P" 2>&1)
check "a new Diatom: the resident one stopped first, then the launcher; both run the new build" \
	'[ "$(head -1 "$D/kills")" = "kill -TERM diatom" ] && grep -q "new diatom" "$D/proc/$(cat "$D/pids/diatom")/exe" &&
	 grep -q "slot launcher" "$D/proc/$(cat "$D/pids/zyperos.elf")/exe" && said "diatom now runs that build"'
fresh; build
echo "new eq-port.sh" > "$P/eq-port.sh"
out=$(dep deploy "$P" 2>&1)
check "only a script a port reads: nothing restarted" '[ ! -e "$D/kills" ] && said "nothing to restart" && grep -q new "$D/card/ZyperOS/eq-port.sh"'
fresh; build
echo "new launch.sh" > "$P/launch.sh"
out=$(dep deploy "$P" 2>&1); rc=$?
check "a new launch.sh restarts the device, and the deploy waits for its next boot ($rc)" \
	'[ $rc -eq 0 ] && [ "$(cat "$D/kills")" = reboot ] && [ "$(cat "$D/proc/sys/kernel/random/boot_id")" != boot-1 ] && said "restarting the device"'
rm -f "$D/kills"
dep restore > /dev/null 2>&1
check "and its restore does too, with the slot's launch.sh back" '[ "$(cat "$D/kills")" = reboot ] && cardsame'

# The system's files: a stand-in bound in init's namespace, gone at restore
fresh; build; own_ns
out=$(dep deploy --system zyperos_eq.so "$P" 2>&1); rc=$?
check "--system: the ports' equalizer bound over the image's, in init's namespace ($rc)" \
	'[ $rc -eq 0 ] && [ "$(cut -f2 "$D/binds")" = "$D/root/usr/lib/ladspa/zyperos_eq.so" ] &&
	 grep -q "new eq" "$(cut -f1 "$D/binds")" && grep -q "image eq" "$D/root/usr/lib/ladspa/zyperos_eq.so" &&
	 grep -q "^mount -o bind .* ns=init$" "$D/mount-calls" && grep -q "initns mount -o bind" "$D/initns-calls"'
check "the card untouched, nothing restarted, and the loader asked about it too" \
	'cardsame && [ ! -e "$D/kills" ] && grep -q "zyperos_eq.so$" "$D/ld-asked" && said "stands in for the system"'
out=$(dep deploy --system zyperos_eq.so "$P" 2>&1)
check "deployed again: the bound one is the same, nothing sent" 'said "nothing sent" && [ "$(grep -c . "$D/calls-scp")" = 1 ]'
elf "$B/system/usr/lib/ladspa/zyperos_eq.so" "newer eq"
out=$(dep deploy --system zyperos_eq.so "$P" 2>&1)
check "a newer one: the last stand-in taken off first, one bind left" \
	'[ "$(grep -c . "$D/binds")" = 1 ] && grep -q "newer eq" "$(cut -f1 "$D/binds")" && grep -q "^umount .* ns=init$" "$D/mount-calls"'
out=$(dep restore --status 2>&1)
check "restore --status says what stands in" 'said "standing in for the system" && said "/usr/lib/ladspa/zyperos_eq.so"'
out=$(dep restore 2>&1); rc=$?
check "restore takes it off: the system's own again, /tmp's copy gone" \
	'[ $rc -eq 0 ] && [ ! -s "$D/binds" ] && [ ! -e "$D/systmp" ] && said "the system'"'"'s own again"'
fresh; build
out=$(dep deploy --system gauge "$P" 2>&1)
check "gauge: bound, init's gauge stopped and started again on the stand-in" \
	'grep -q "kill -TERM gauge" "$D/kills" && grep -q "new gauge" "$D/proc/$(cat "$D/pids/gauge")/exe" && said "gauge now runs that build"'
fresh; build
echo "new launch.sh" > "$P/launch.sh"
out=$(dep deploy --system asound.conf "$P" 2>&1); rc=$?
check "with a new boot as well, the stand-in bound once the device is back ($rc)" \
	'[ $rc -eq 0 ] && grep -q reboot "$D/kills" && [ "$(cut -f2 "$D/binds")" = "$D/root/etc/asound.conf" ] &&
	 grep -q "new asound" "$(cut -f1 "$D/binds")" && said "binding the system'"'"'s files again"'
out=$(dep deploy --system nosuch "$P" 2>&1); rc=$?
check "a piece the build has none of is refused, naming the ones there are" '[ $rc -eq 1 ] && said "no system piece nosuch" && said "zyperos_eq.so"'
fresh; build; own_ns
rm -f "$D/initns-local"
out=$(dep deploy --system gauge "$P" 2>&1); rc=$?
check "in a namespace of its own with no initns built: refused, nothing sent" '[ $rc -eq 1 ] && said "make px2-programs" && [ ! -e "$D/calls-scp" ]'
cat > "$D/initns-local" <<'EOF'
#!/bin/sh
echo "initns $*" >> "$FAKE_DIR/initns-calls"
FAKE_NS_SEEN=init exec "$@"
EOF
chmod +x "$D/initns-local"

# Kept on another system: a new slot (or an update) since the first deploy
fresh; build
elf "$P/zyperos.elf" "new launcher"
dep deploy "$P" > /dev/null 2>&1
echo "1.1.0-rc.10" > "$D/seed/ZyperOS/VERSION"
elf "$D/seed/ZyperOS/zyperos.elf" "rc.10 launcher"
index "$D/seed/ZyperOS"
elf "$P/zyperos.elf" "newer launcher"
out=$(dep deploy "$P" 2>&1); rc=$?
check "kept on another system: a deploy is refused (7)" '[ $rc -eq 7 ] && said "restore --from-system"'
out=$(dep restore 2>&1); rc=$?
check "and so is a plain restore (7)" '[ $rc -eq 7 ] && grep -q "new launcher" "$D/card/ZyperOS/zyperos.elf"'
out=$(dep restore --from-system 2>&1); rc=$?
check "restore --from-system puts back this system's own copy" \
	'[ $rc -eq 0 ] && grep -q "rc.10 launcher" "$D/card/ZyperOS/zyperos.elf" && said "restored zyperos.elf (the system'"'"'s own)" && [ ! -e "$D/card/.px2-deploy" ]'

# Kept by an older deploy, which wrote down no system (the owner's card had
# one, 2026-11-21): a restore would put a version long gone back over this one
fresh; build
mkdir -p "$D/card/.px2-deploy/orig"
echo "a launcher from long ago" > "$D/card/.px2-deploy/orig/zyperos.elf"
echo "2026-11-20 00:52:16" > "$D/card/.px2-deploy/since"
rm -rf "$D/before"; cp -R "$D/card" "$D/before"
elf "$P/zyperos.elf" "new launcher"
out=$(dep deploy "$P" 2>&1); rc=$?
check "files kept with no system written down: a deploy is refused (7), nothing sent" \
	'[ $rc -eq 7 ] && said "of a system nobody wrote down" && said "nothing was sent" && cardsame && [ ! -e "$D/calls-scp" ]'
out=$(dep restore 2>&1); rc=$?
check "and a plain restore too, the long-gone launcher left where it is" '[ $rc -eq 7 ] && cardsame'
out=$(dep restore --status 2>&1)
check "restore --status says so" 'said "an older tools/px2-deploy.sh kept them"'
out=$(dep restore --from-system 2>&1); rc=$?
check "restore --from-system: this system's own launcher, and the folder gone" \
	'[ $rc -eq 0 ] && grep -q "slot launcher" "$D/card/ZyperOS/zyperos.elf" && [ ! -e "$D/card/.px2-deploy" ]'

FAKE_CARD=249737216 dep deploy "$P" > /dev/null 2>&1; rc=$?
check "deploy holds the card to the test card like the rest" '[ $rc -eq 1 ]'

# find: zyperos.local, or a sweep for dropbear's greeting and the key's two
# reads, here on this computer's loopback with stand-ins for the Pixel's
# dropbear and for ssh
cat > "$D/find-ssh" <<'EOF'
#!/bin/sh
for a in "$@"; do case $a in root@*) host=${a#root@} ;; esac; done
echo "$host" >> "$FAKE_DIR/find-asked"
[ "$host" = 127.0.0.1 ] || exit 255
printf '62333952\n1.1.0-rc.9\n'
EOF
chmod +x "$D/find-ssh"
rm -f "$D/find-port" "$D/find-asked"
python3 - "$D/find-port" <<'EOF' &
import socket, sys
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('127.0.0.1', 0))
s.listen(8)
s.settimeout(30)
open(sys.argv[1], 'w').write(str(s.getsockname()[1]))
try:
    while True:
        c, _ = s.accept()
        c.sendall(b'SSH-2.0-dropbear_2024.86\r\n')
        c.close()
except OSError:
    pass
EOF
fake_dropbear=$!
i=0
while [ ! -s "$D/find-port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
out=$(PX2_SSH="$D/find-ssh" python3 tools/px2-find.py --no-name --net 127.0.0.0/30 --port "$(cat "$D/find-port")" 2>&1); rc=$?
kill $fake_dropbear 2> /dev/null
wait $fake_dropbear 2> /dev/null
check "find: the Pixel by dropbear's greeting and its key, with the line for PX2_HOST ($rc)" \
	'[ $rc -eq 0 ] && said "127.0.0.1: ZyperOS 1.1.0-rc.9, the 32 GB test card" && said "PX2_HOST=127.0.0.1 PX2_PORT="'
check "and the key tried only where dropbear greeted" '[ "$(cat "$D/find-asked")" = 127.0.0.1 ]'
for args in "deploy a b" "deploy --x" "deploy --system" "deploy --system a --system b" "deploy --system --no-restart" \
	"restore now" "restore --status x" "restore --status --no-restart" "find x"; do
	# shellcheck disable=SC2086
	px2 $args > /dev/null 2>&1; rc=$?
	[ $rc -eq 2 ] || bad "'tools/px2 $args' exits 2, not $rc"
done

if [ $fails -gt 0 ]; then
	echo "$fails failed"
	exit 1
fi
echo "ok: tools/px2 refuses wrong uses first, holds the card to the test card, lets its lock go however it ends; deploy sends only what changed, refuses what the system's loader cannot bind, restarts only what must and sees it run, and restore gives the card and the system back as they were; find finds the Pixel"
