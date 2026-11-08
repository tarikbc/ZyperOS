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

# The default lock is the Pixel 2's own only for the Pixel 2
check "the Pixel 2 at 10.42.0.1:22 takes /tmp/px2-lock, anything else its own" \
	'grep -q "elif \[ \"\$HOST:\$PORT\" = 10.42.0.1:22 \]; then LOCK=/tmp/px2-lock$" "$PX2" &&
	 grep -q "else LOCK=/tmp/px2-lock-\$HOST-\$PORT$" "$PX2"'

if [ $fails -gt 0 ]; then
	echo "$fails failed"
	exit 1
fi
echo "ok: tools/px2 refuses wrong uses first, holds the card to the test card, and lets its lock go however it ends"
