#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Put a build of ZyperOS on a GKD Pixel 2 for a test, without a new image and
# without taking the card out: whatever differs from what the card has, then
# only what has to start again does, and the deploy says what now runs.
#
#   tools/px2-deploy.sh [--system PIECES] [--no-restart] DIR
#   tools/px2-deploy.sh --restore [--from-system] [--no-restart]
#   tools/px2-deploy.sh --status
#
# DIR is a ZyperOS/ folder: make px2-programs' build/px2/programs/ZyperOS
# (the programs and the card's scripts), make px2-payload's
# build/px2/payload/ZyperOS (all of it, cores too), or plastron's
# /usr/share/zyperos/card/ZyperOS. --system also stands in for the system
# image's own files a fix changes (make px2-programs builds them into
# build/px2/system): PIECES is a comma list of their names (zyperos_eq.so,
# gauge, asound.conf) or "all". --restore puts back the card as it was before
# the first deploy since the last restore, and the system's own files;
# --from-system takes the card's files from the system image's own copy of
# ZyperOS instead of what the deploys kept. --status says what a restore
# would put back. --no-restart puts the files and restarts nothing.
#
# tools/px2 deploy and tools/px2 restore are the same under the device's lock
# and its card check: use those when another session may use the device.
#
# Needs a development build of plastron (its SSH key in the image), or
# Developer Mode with a key, and the device at 10.42.0.1 (PX2_HOST: its
# Wi-Fi address, docs/building.md), on SSH's port 22 (PX2_PORT: a QEMU VM's
# forwarded port, tools/qemu-run.sh). A few round trips whatever the number
# of files, so a deploy over the Pixel's slow Wi-Fi costs the bytes, not the
# files: the card's checksums of the files DIR has, then the files that
# differ in one copy to /tmp, then one step on the device that puts them in
# place, then the restart.
#
# The step on the device changes nothing until every file is checked:
#  - Each as it was sent (its SHA-256).
#  - Each program and library against the running system, by the system's
#    own dynamic loader, as ldd -r asks it (LD_TRACE_LOADED_OBJECTS: the
#    loader maps the program and its libraries, binds every symbol and stops
#    before any of the program's code runs). A library the system lacks, or a
#    symbol or symbol version it does not have, is a refusal that names them,
#    with the SDK the programs were built against (build/px2/*/BUILT): the
#    programs would not start there. The SDK's id itself cannot be compared,
#    as no image records the one it would pack to, and a release's never
#    equals a development build's (its output path is in its binaries).
#  - Nothing a restart would cut short: a game open in Diatom, a port, or the
#    card lent to a computer (Flash Drive, never, --no-restart or not).
# Then each file goes onto the card under a new name and is renamed over the
# old, so a cable pulled halfway leaves the old file or the new, never half.
#
# What a deploy replaces is kept first, on the card in /mnt/SDCARD/.px2-deploy
# (outside ZyperOS/, so nothing there reads it): the card's own file the first
# time a deploy replaces it, the name of each file a deploy added, and which
# system it was kept on (the image's own ZyperOS, by the SHA-256 of its
# index, /usr/share/zyperos/card/ZyperOS/.files). A later deploy keeps what
# the first one kept, so --restore always goes back to the card from before
# the session's first deploy, says for each file whether it is the system's
# own copy, and forgets it. Kept on another system (an update or a new slot
# since), or by an older deploy that wrote no system down, is a refusal
# until --restore --from-system, which takes the files from the image's own
# ZyperOS (or the folder moved aside by hand). PX2_DEPLOY_KEEP=0 keeps
# nothing (a release's payload meant to stay).
#
# The system's files are in the read-only image, so --system binds a copy in
# /tmp over each one (in init's mount namespace, with build/px2/initns when
# SSH has a namespace of its own, as Developer Mode's does): gone at the next
# restart of the device, or at --restore. Only a file the image has can be
# stood in for; a new one needs an image.
#
# Then what has to start again, and nothing else, and never with zyperos
# stop, which would end SSH with nothing to start ZyperOS again:
#  - launch.sh, device.sh, zyhome, or what launch.sh copies at its start
#    (scummvm/, ppsspp/, freej2me/, prboom/): the device restarts, as the
#    shell running launch.sh keeps reading the file it opened; a deploy with
#    --system binds its files again once SSH is back.
#  - zyperos.elf, or anything else the launcher reads: the launcher, as the
#    charging screen's restart does it (/tmp/zyperos_restart, then TERM:
#    launch.sh starts it again at once and counts no failure).
#  - diatom or a core: Diatom too (stopped first, so launch.sh starts the new
#    one before the launcher). mp4d, p2d: that program, which the launcher
#    starts again when it needs it. gauge: gauge, which init starts again.
#    /etc/asound.conf: what plays sound.
#  - eq-port.sh, portkeys, pico8/, openbor/, portmaster/, zyperos-update,
#    the ports' equalizer: nothing; they are read at the next port (or
#    System Update).
# After a restart the deploy waits for SSH and says which build each
# restarted program runs.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
HOST=${PX2_HOST:-10.42.0.1}
PORT=${PX2_PORT:-22}
KEY=${PX2_KEY:-$HOME/.ssh/zyperos_pixel2}
SSH_BIN=${PX2_SSH:-ssh}
SCP_BIN=${PX2_SCP:-scp}
# The device's places and commands. make check-px2's stand-ins
# (tools/px2-check.sh) put each in a folder on this computer
CARD=${PX2_CARD:-/mnt/SDCARD}
DTMP=${PX2_DEVICE_TMP:-/tmp/px2-deploy}
SYST=${PX2_DEVICE_SYSTMP:-/tmp/px2-deploy-sys}
FLAGS=${PX2_DEVICE_FLAGS:-/tmp}
SEED=${PX2_SEED:-/usr/share/zyperos/card}
PROC=${PX2_PROC:-/proc}
ROOT=${PX2_ROOT:-}
RTLD=${PX2_RTLD:-}
# What asks the loader to bind every symbol and stop before the program runs
# (ldd -r's). make check-px2 renames them for its stand-in, a script, which
# a glibc computer's own loader would otherwise trace instead of running
LDENV=${PX2_LDENV:-LD_TRACE_LOADED_OBJECTS=1 LD_WARN=yes LD_BIND_NOW=yes}
KILL=${PX2_KILL:-kill}
# This computer's: the system's files and the namespace helper a build left
SYSDIR=${PX2_SYSTEM_DIR:-$REPO/build/px2/system}
INITNS=${PX2_INITNS:-$REPO/build/px2/initns}
WAIT=${PX2_DEPLOY_WAIT:-90}
KEEP=${PX2_DEPLOY_KEEP:-1}
DEST=$CARD/ZyperOS
SAVE=$CARD/.px2-deploy
# dropbear makes a new host key at every boot of a read-only root: no key to check
OPTS="-o ConnectTimeout=8 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
SSH="$SSH_BIN $OPTS -p $PORT -i $KEY root@$HOST"
# Compressed off the cable: over Wi-Fi the radio is what is slow
Z=-C
case $HOST in 10.42.0.1 | 127.0.0.1 | localhost) Z= ;; esac
SCP="$SCP_BIN -O -q -r $Z $OPTS -P $PORT -i $KEY"
TAB=$(printf '\t')

die() { echo "px2-deploy: $*" >&2; exit 1; }
usage() { sed -n '6,8p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2; }

MODE=deploy SRC= PIECES= NORESTART=0 FROMSYS=0
while [ $# -gt 0 ]; do
	case $1 in
	--restore) MODE=restore ;;
	--status) MODE=status ;;
	--from-system) FROMSYS=1 ;;
	--no-restart) NORESTART=1 ;;
	--system) [ $# -ge 2 ] || usage; PIECES=$2; shift ;;
	-*) usage ;;
	*) [ -z "$SRC" ] || usage; SRC=$1 ;;
	esac
	shift
done
case $MODE in
deploy) [ -n "$SRC" ] && [ "$FROMSYS" = 0 ] || usage ;;
restore) [ -z "$SRC" ] && [ -z "$PIECES" ] || usage ;;
status) [ -z "$SRC" ] && [ -z "$PIECES" ] && [ "$FROMSYS" = 0 ] && [ "$NORESTART" = 0 ] || usage ;;
esac

W=$(mktemp -d "${TMPDIR:-/tmp}/px2-deploy.XXXXXX") || die "no scratch folder"
trap 'rm -rf "$W"' EXIT
trap 'rm -rf "$W"; exit 130' INT
trap 'rm -rf "$W"; exit 143' TERM

if command -v sha256sum > /dev/null 2>&1; then SHA=sha256sum; else SHA="shasum -a 256"; fi
sha() { $SHA "$1" | cut -d' ' -f1; }
# A string as one word for the device's shell
q() { printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"; }
size() {
	n=$(wc -c < "$1" | tr -d ' ')
	if [ "$n" -ge 1048576 ]; then echo "$((n * 10 / 1048576 / 10)).$((n * 10 / 1048576 % 10)) MB"
	elif [ "$n" -ge 1024 ]; then echo "$((n / 1024)) KB"; else echo "$n B"; fi
}

# --- on the device -----------------------------------------------------------
#
# Each step is one shell script, sent on ssh's standard input (dropbear takes
# a command line of 9000 bytes at most): the places above, the helpers
# below, then the step.

device_head() {
	echo "CARD=$(q "$CARD") DEST=$(q "$DEST") SAVE=$(q "$SAVE") DTMP=$(q "$DTMP") SYST=$(q "$SYST")"
	echo "FLAGS=$(q "$FLAGS") SEED=$(q "$SEED") PROC=$(q "$PROC") ROOT=$(q "$ROOT") RTLD=$(q "$RTLD")"
	echo "KILL=$(q "$KILL") KEEP=$(q "$KEEP") NORESTART=$NORESTART FROMSYS=$FROMSYS LDENV=$(q "$LDENV")"
	cat <<'EOF'
TAB=$(printf '\t')
NL='
'
# The image's own ZyperOS, by its index: the system a card's kept files
# belong to
sysid() {
	for f in "$SEED/ZyperOS/.files" "$SEED/ZyperOS/VERSION"; do
		if [ -f "$f" ]; then sha256sum "$f" | cut -c1-12; return; fi
	done
	echo unknown
}
# What a restart would cut short, a line each
busy() {
	[ -n "$(pidof usbdrive 2> /dev/null)" ] && echo "usbdrive"
	[ -n "$(pidof portkeys 2> /dev/null)" ] && echo "port"
	if [ -f "$FLAGS/diatom-now" ]; then
		p=$(sed -n 's/^pid=//p' "$FLAGS/diatom-now")
		r=$(sed -n 's/^rom=//p' "$FLAGS/diatom-now")
		[ -n "$p" ] && [ -d "$PROC/$p" ] && echo "game $r"
	fi
	[ -n "$(pidof zyperos.elf 2> /dev/null)" ] || echo "nolauncher"
	return 0
}
# Init's mount namespace, or one of its own (Developer Mode's SSH)
ns() {
	if [ "$(readlink "$PROC/self/ns/mnt" 2> /dev/null)" = "$(readlink "$PROC/1/ns/mnt" 2> /dev/null)" ]; then
		echo init
	else
		echo own
	fi
}
# The command that runs $@ in init's namespace: nothing more in it
in_init() { if [ "$(ns)" = own ]; then "$SYST/initns" "$@"; else "$@"; fi; }
# The seed index's SHA-256 for a ZyperOS/ path, empty when it has none
seed_sum() { awk -v f="$1" '{ h = $1; sub(/^[^ ]+ [^ ]+ /, ""); if ($0 == f) { print h; exit } }' "$SEED/ZyperOS/.files" 2> /dev/null; }
# Whether the restored file at ZyperOS/$1 is the system's own copy
verdict() {
	s=$(seed_sum "$1")
	if [ -z "$s" ] && [ ! -f "$SEED/ZyperOS/$1" ]; then echo "not in the system's ZyperOS"
	elif [ ! -f "$DEST/$1" ]; then echo "gone"
	elif [ -z "$s" ]; then s=$(sha256sum "$SEED/ZyperOS/$1" | cut -d' ' -f1); [ "$s" = "$(sha256sum "$DEST/$1" | cut -d' ' -f1)" ] && echo "the system's own" || echo "the card's own, not the system's"
	elif [ "$s" = "$(sha256sum "$DEST/$1" | cut -d' ' -f1)" ]; then echo "the system's own"
	else echo "the card's own, not the system's"; fi
}
EOF
}

# Facts and the device's checksums of the files named in $W/rels (ZyperOS/
# paths) and $W/abs (the system's)
probe() {
	{
		device_head
		cat <<'EOF'
rm -rf "$DTMP"
echo "ns=$(ns)"
echo "sysid=$(sysid)"
v=; [ -f "$SEED/ZyperOS/VERSION" ] && read -r v < "$SEED/ZyperOS/VERSION"; echo "seedver=$v"
v=; [ -f "$DEST/VERSION" ] && read -r v < "$DEST/VERSION"; echo "cardver=$v"
r=$RTLD
if [ -z "$r" ]; then
	for c in /lib/ld-linux-aarch64.so.1 /lib/ld-linux-armhf.so.3 /lib64/ld-linux-x86-64.so.2; do
		if [ -e "$c" ]; then r=$c; break; fi
	done
fi
echo "rtld=$r"
busy | sed 's/^/busy=/'
if [ -d "$SAVE" ]; then
	echo "kept=$(cat "$SAVE/since" 2> /dev/null)"
	echo "keptsys=$(cat "$SAVE/system" 2> /dev/null)"
fi
[ -f "$SYST/binds" ] && sed "s/^/bind$TAB/" "$SYST/binds"
[ -f "$PROC/sys/kernel/random/boot_id" ] && echo "boot=$(cat "$PROC/sys/kernel/random/boot_id")"
while IFS= read -r rel; do
	[ -f "$DEST/$rel" ] && printf 'sum\t%s\t%s\n' "$(sha256sum "$DEST/$rel" | cut -d' ' -f1)" "$rel"
done <<'PX2_LIST'
EOF
		cat "$W/rels"
		echo PX2_LIST
		echo "while IFS= read -r abs; do"
		cat <<'EOF'
	if [ -f "$ROOT$abs" ]; then printf 'syssum\t%s\t%s\n' "$(sha256sum "$ROOT$abs" | cut -d' ' -f1)" "$abs"
	else printf 'sysnone\t-\t%s\n' "$abs"; fi
done <<'PX2_LIST'
EOF
		cat "$W/abs"
		echo PX2_LIST
	} > "$W/probe.sh"
	$SSH 'sh -s' < "$W/probe.sh" > "$W/facts" || die "cannot reach $HOST:$PORT over SSH"
}
fact() { sed -n "s/^$1=//p" "$W/facts" | head -1; }

# Puts what $W/stage holds in place: $W/card.list (SHA-256, flags, ZyperOS/
# path) and $W/sys.list (SHA-256, flags, the system's path)
apply_script() {
	device_head
	echo "RTLD=$(q "$(fact rtld)")"
	echo "cat > \"\$DTMP/card.list\" <<'PX2_LIST'"
	cat "$W/card.list"
	echo PX2_LIST
	echo "cat > \"\$DTMP/sys.list\" <<'PX2_LIST'"
	cat "$W/sys.list"
	echo PX2_LIST
	cat <<'EOF'
cd /
fail() { echo "px2-deploy: $1" >&2; rm -rf "$DTMP"; exit "${2:-1}"; }
# Every file as it was sent: a link cut halfway changes nothing
while IFS="$TAB" read -r sha flags rel; do
	[ "$(sha256sum "$DTMP/card/$rel" 2> /dev/null | cut -d' ' -f1)" = "$sha" ] ||
		fail "$rel arrived damaged; nothing was changed" 5
done < "$DTMP/card.list"
while IFS="$TAB" read -r sha flags abs; do
	[ "$(sha256sum "$DTMP/sys$abs" 2> /dev/null | cut -d' ' -f1)" = "$sha" ] ||
		fail "$abs arrived damaged; nothing was changed" 5
done < "$DTMP/sys.list"
# Every program and library against this system, as its loader sees them
[ -n "$RTLD" ] && [ -e "$RTLD" ] ||
	fail "this system has no dynamic loader to ask (${RTLD:-none found}); nothing was changed" 6
abi=
loader() {
	# shellcheck disable=SC2086
	out=$(env $LDENV "$RTLD" "$2" 2>&1)
	miss=$(printf '%s\n' "$out" | grep -e 'not found' -e 'undefined symbol' -e 'symbol lookup error' -e 'cannot open' |
	       sed 's/^[[:space:]]*/    /')
	[ -z "$miss" ] || abi="$abi  $1$NL$miss$NL"
}
while IFS="$TAB" read -r sha flags rel; do
	case $flags in *e*) loader "$rel" "$DTMP/card/$rel" ;; esac
done < "$DTMP/card.list"
while IFS="$TAB" read -r sha flags abs; do
	case $flags in *e*) loader "$abs" "$DTMP/sys$abs" ;; esac
done < "$DTMP/sys.list"
if [ -n "$abi" ]; then
	printf '%s' "$abi" >&2
	fail "built against what this system does not have; nothing was changed" 6
fi
# Nothing a restart would cut short; the card never while a computer has it
b=$(busy)
case $b in *usbdrive*) fail "the card is a computer's now (Flash Drive); nothing was changed" 4 ;; esac
if [ "$NORESTART" != 1 ] && [ -n "$b" ]; then
	printf '%s\n' "$b" | sed 's/^/busy=/' >&2
	fail "busy; nothing was changed" 4
fi
# The files kept belong to the system they were kept on
id=$(sysid)
if [ "$KEEP" = 1 ] && [ -d "$SAVE" ] && [ "$(cat "$SAVE/system" 2> /dev/null)" != "$id" ]; then
	fail "the card's files kept in $SAVE (since $(cat "$SAVE/since" 2> /dev/null)) are $(cat "$SAVE/system" 2> /dev/null | sed 's/^\(..*\)$/another system'"'"'s (\1)/; s/^$/of a system nobody wrote down (an older tools\/px2-deploy.sh kept them)/'); this one is $id: tools/px2 restore --from-system first, or move the folder aside; nothing was changed" 7
fi
# The system's files: a copy in /tmp bound over each, in init's namespace
if [ -s "$DTMP/sys.list" ]; then
	mkdir -p "$SYST/files"
	if [ -f "$DTMP/initns" ]; then cp "$DTMP/initns" "$SYST/initns" && chmod +x "$SYST/initns"; fi
	[ "$(ns)" = init ] || [ -x "$SYST/initns" ] ||
		fail "SSH has a mount namespace of its own here and no initns came (make px2-programs builds it); nothing was changed" 8
	while IFS="$TAB" read -r sha flags abs; do
		[ -f "$ROOT$abs" ] || fail "the system has no $abs to stand in for: a new file needs an image" 8
		if [ -f "$SYST/binds" ] && awk -F"$TAB" -v a="$abs" '$2 == a { f = 1 } END { exit !f }' "$SYST/binds"; then
			in_init umount "$ROOT$abs" || fail "could not take the last stand-in off $abs" 8
			awk -F"$TAB" -v a="$abs" '$2 != a' "$SYST/binds" > "$SYST/binds.new" && mv -f "$SYST/binds.new" "$SYST/binds"
		fi
		mkdir -p "$SYST/files${abs%/*}"
		cp "$DTMP/sys$abs" "$SYST/files$abs.new" && mv -f "$SYST/files$abs.new" "$SYST/files$abs" ||
			fail "no room in $SYST for $abs" 8
		case $flags in *x*) chmod +x "$SYST/files$abs" ;; esac
		in_init mount -o bind "$SYST/files$abs" "$ROOT$abs" || fail "could not bind $abs" 8
		printf '%s\t%s\n' "$sha" "$abs" >> "$SYST/binds"
		echo "bound$TAB$abs"
	done < "$DTMP/sys.list"
fi
# The card's own copy of each file first, the first time only, and the name
# of each one it did not have; on the card before anything is replaced
if [ "$KEEP" = 1 ] && [ -s "$DTMP/card.list" ]; then
	while IFS="$TAB" read -r sha flags rel; do
		[ ! -e "$SAVE/orig/$rel" ] && ! grep -qxF "$rel" "$SAVE/added" 2> /dev/null || continue
		mkdir -p "$SAVE"
		[ -f "$SAVE/since" ] || date '+%Y-%m-%d %H:%M:%S' > "$SAVE/since"
		[ -s "$SAVE/system" ] || echo "$id" > "$SAVE/system"
		if [ -e "$DEST/$rel" ]; then
			case $rel in */*) mkdir -p "$SAVE/orig/${rel%/*}" ;; *) mkdir -p "$SAVE/orig" ;; esac
			cp -p "$DEST/$rel" "$SAVE/orig/$rel" || fail "could not keep the card's $rel; nothing was replaced" 8
		else
			echo "$rel" >> "$SAVE/added"
		fi
	done < "$DTMP/card.list"
	sync
fi
while IFS="$TAB" read -r sha flags rel; do
	case $rel in */*) mkdir -p "$DEST/${rel%/*}" ;; *) mkdir -p "$DEST" ;; esac
	cp "$DTMP/card/$rel" "$DEST/$rel.px2-new" && mv -f "$DEST/$rel.px2-new" "$DEST/$rel" ||
		fail "could not put $rel on the card: what is in place stays, tools/px2 restore puts the rest back" 8
	case $flags in *x*) chmod +x "$DEST/$rel" ;; esac
	echo "put$TAB$rel"
done < "$DTMP/card.list"
rm -rf "$DTMP"
sync
EOF
}

# The restart a set of changed paths needs ($W/changed: ZyperOS/ paths and
# the system's /paths, one a line): words in $W/plan
plan() {
	reboot= diatom= mp4d= p2d= gauge= launcher= later=
	while IFS= read -r p; do
		case $p in
		launch.sh | device.sh | zyhome | scummvm/* | ppsspp/* | freej2me/* | prboom/*) reboot=1 ;;
		diatom | cores/*) diatom=1 launcher=1 ;;
		mp4d) mp4d=1 ;;
		p2d) p2d=1 ;;
		eq-port.sh | portkeys | zyperos-update | bt-alsa.sh | pico8/* | openbor/* | portmaster/* | .files) later="$later $p" ;;
		/etc/asound.conf) diatom=1 mp4d=1 launcher=1 ;;
		/usr/sbin/gauge) gauge=1 ;;
		/*) later="$later $p" ;;
		*) launcher=1 ;;
		esac
	done < "$W/changed"
	{
		if [ -n "$reboot" ]; then echo reboot
		else
			[ -n "$diatom" ] && echo diatom
			[ -n "$mp4d" ] && echo mp4d
			[ -n "$p2d" ] && echo p2d
			[ -n "$gauge" ] && echo gauge
			[ -n "$launcher" ] && echo launcher
		fi
		true
	} > "$W/plan"
	LATER=$later
}

# Runs $W/plan's restarts, detached on the device: a launcher's exit ends
# Developer Mode's SSH sessions, this one too
restart() {
	[ -s "$W/plan" ] || return 0
	{
		device_head
		echo "PLAN='$(tr '\n' ' ' < "$W/plan")'"
		cat <<'EOF'
cat > "$FLAGS/px2-restart.sh" <<'PX2_STEPS'
stop_wait() {
	for p in $*; do $KILL -TERM "$p" 2> /dev/null; done
	i=0
	while [ $i -lt 50 ]; do
		left=
		for p in $*; do [ -d "$PROC/$p" ] && left=1; done
		[ -z "$left" ] && return 0
		sleep 0.1
		i=$((i + 1))
	done
	for p in $*; do $KILL -KILL "$p" 2> /dev/null; done
}
case " $PLAN " in *" reboot "*) sync; reboot; exit 0 ;; esac
case " $PLAN " in *" diatom "*)
	p=$(cat "$FLAGS/diatom.pid" 2> /dev/null)
	[ -n "$p" ] && [ "$(cat "$PROC/$p/comm" 2> /dev/null)" = diatom ] && stop_wait "$p" ;;
esac
case " $PLAN " in *" mp4d "*) stop_wait $(pidof mp4d 2> /dev/null) ;; esac
case " $PLAN " in *" p2d "*) stop_wait $(pidof p2d 2> /dev/null) ;; esac
case " $PLAN " in *" gauge "*) stop_wait $(pidof gauge 2> /dev/null) ;; esac
case " $PLAN " in *" launcher "*)
	: > "$FLAGS/zyperos_restart"
	for p in $(pidof zyperos.elf 2> /dev/null); do $KILL -TERM "$p"; done ;;
esac
rm -f "$FLAGS/px2-restart.sh"
PX2_STEPS
export PLAN KILL PROC FLAGS
if command -v setsid > /dev/null 2>&1; then
	setsid sh "$FLAGS/px2-restart.sh" < /dev/null > /dev/null 2>&1 &
else
	sh "$FLAGS/px2-restart.sh"
fi
EOF
	} > "$W/restart.sh"
	$SSH 'sh -s' < "$W/restart.sh" || true
}

# After a restart: SSH back, then each restarted program's build against the
# file it should run, until they agree or PX2_DEPLOY_WAIT seconds pass
verify() {
	[ -s "$W/plan" ] || return 0
	{
		device_head
		echo "PLAN='$(tr '\n' ' ' < "$W/plan")'"
		cat <<'EOF'
same() { [ -n "$1" ] && [ -f "$2" ] && [ "$(sha256sum "$PROC/$1/exe" 2> /dev/null | cut -d' ' -f1)" = "$(sha256sum "$2" | cut -d' ' -f1)" ]; }
look() {
	p=$2
	if [ -z "$p" ]; then printf 'none\t%s\n' "$1"
	elif same "$p" "$3"; then printf 'new\t%s\t%s\n' "$1" "$p"
	else printf 'old\t%s\t%s\n' "$1" "$p"; fi
}
[ -f "$PROC/sys/kernel/random/boot_id" ] && echo "boot=$(cat "$PROC/sys/kernel/random/boot_id")"
for w in $PLAN; do
	case $w in
	launcher | reboot) look zyperos.elf "$(pidof zyperos.elf 2> /dev/null | cut -d' ' -f1)" "$DEST/zyperos.elf" ;;
	diatom)
		p=$(cat "$FLAGS/diatom.pid" 2> /dev/null)
		[ -n "$p" ] && [ "$(cat "$PROC/$p/comm" 2> /dev/null)" = diatom ] || p=
		look diatom "$p" "$DEST/diatom" ;;
	gauge)
		f=$ROOT/usr/sbin/gauge
		[ -f "$SYST/files/usr/sbin/gauge" ] && f=$SYST/files/usr/sbin/gauge
		look gauge "$(pidof gauge 2> /dev/null | cut -d' ' -f1)" "$f" ;;
	esac
done
EOF
	} > "$W/verify.sh"
	was_boot=$(fact boot)
	t0=$(date +%s)
	[ "$(head -1 "$W/plan")" = reboot ] && sleep "${PX2_DEPLOY_REBOOT_PAUSE:-5}"
	: > "$W/runs"
	while :; do
		if $SSH 'sh -s' < "$W/verify.sh" > "$W/runs.new" 2> /dev/null; then
			mv -f "$W/runs.new" "$W/runs"
			boot=$(sed -n 's/^boot=//p' "$W/runs")
			if [ "$(head -1 "$W/plan")" != reboot ] || [ -z "$was_boot" ] || [ "$boot" != "$was_boot" ]; then
				grep -v "^none${TAB}diatom" "$W/runs" | grep -q -e '^old' -e '^none' || break
			fi
		fi
		[ $(($(date +%s) - t0)) -lt "$WAIT" ] || break
		sleep 2
	done
	if ! $SSH true < /dev/null 2> /dev/null; then
		echo "px2-deploy: no SSH $WAIT s after the restart. No SSH while ping answers does not mean ZyperOS is down: look at the Pixel, or try again in a while" >&2
		return 9
	fi
	rc=0
	grep -q -e '^new' -e '^old' -e '^none' "$W/runs" ||
		{ echo "px2-deploy: no answer from the device about what runs after $WAIT s" >&2; return 9; }
	while IFS="$TAB" read -r st name pid; do
		case $st in
		new) echo "px2-deploy: $name now runs that build (pid $pid)" ;;
		old) echo "px2-deploy: $name still runs another build (pid $pid) after $WAIT s" >&2; rc=9 ;;
		none) [ "$name" = diatom ] && echo "px2-deploy: no resident Diatom yet: the launcher starts one for the first game" ||
			{ echo "px2-deploy: $name is not running after $WAIT s" >&2; rc=9; } ;;
		esac
	done < "$W/runs"
	return $rc
}

# $W/plan's words as what they restart
plan_names() {
	sed 's/^reboot$/the device/; s/^diatom$/Diatom/; s/^launcher$/ZyperOS/' "$W/plan" | tr '\n' ',' | sed 's/,$//; s/,/, /g'
}
say_plan() {
	if [ ! -s "$W/plan" ]; then
		echo "px2-deploy: nothing to restart"
	elif [ "$NORESTART" = 1 ]; then
		echo "px2-deploy: --no-restart: the files are in place, and run once these start again: $(plan_names)"
	else
		echo "px2-deploy: restarting $(plan_names)"
	fi
	[ -z "$LATER" ] || echo "px2-deploy: read at their next use (a port, System Update), nothing to restart for:$LATER"
	return 0
}

# --- the system's files (--system) ------------------------------------------

# $W/abs and $W/sys.local (SHA-256, flags, path) for the pieces asked for
system_local() {
	: > "$W/abs"
	: > "$W/sys.local"
	[ -n "$PIECES" ] || return 0
	[ -d "$SYSDIR" ] || die "no $SYSDIR: make px2-programs (or make px2-payload) builds the system's files"
	(cd "$SYSDIR" && find . -type f ! -name '._*' ! -name .DS_Store | sed 's|^\.||' | LC_ALL=C sort) > "$W/sys.all"
	: > "$W/asked"
	for want in $(printf '%s' "$PIECES" | tr ',' ' '); do
		if [ "$want" = all ]; then cat "$W/sys.all" >> "$W/asked"; continue; fi
		hit=$(awk -v w="$want" '{ n = $0; sub(/.*\//, "", n) } $0 == w || n == w' "$W/sys.all")
		[ -n "$hit" ] || die "no system piece $want in $SYSDIR (there are: $(sed 's|.*/||' "$W/sys.all" | tr '\n' ' ' | sed 's/ $//'))"
		printf '%s\n' "$hit" >> "$W/asked"
	done
	LC_ALL=C sort -u "$W/asked" > "$W/abs"
	while IFS= read -r abs; do
		printf '%s\t%s\t%s\n' "$(sha "$SYSDIR$abs")" "$(flags "$SYSDIR$abs")" "$abs"
	done < "$W/abs" > "$W/sys.local"
}

# x for a program, e for an ELF file (the loader is asked about it)
flags() {
	f=
	[ -x "$1" ] && f=x
	[ "$(head -c 4 "$1" | od -An -tx1 | tr -d ' \n')" = 7f454c46 ] && f=${f}e
	echo "${f:--}"
}

# --- status, restore ------------------------------------------------------

if [ "$MODE" = status ]; then
	{
		device_head
		cat <<'EOF'
if [ -d "$SAVE" ]; then
	echo "px2-deploy: kept since $(cat "$SAVE/since" 2> /dev/null || echo ?), on system $(cat "$SAVE/system" 2> /dev/null || echo "? (an older tools/px2-deploy.sh kept them)") (this one is $(sysid))"
	[ -d "$SAVE/orig" ] && (cd "$SAVE/orig" && find . -type f | sed 's|^\./|  replaced |')
	[ -f "$SAVE/added" ] && sed 's|^|  added |' "$SAVE/added"
else
	echo "px2-deploy: nothing kept: the card is as no deploy left it"
fi
[ -s "$SYST/binds" ] && echo "px2-deploy: standing in for the system's own, until the next restart:" &&
	awk -F"$TAB" '{ print "  " $2 }' "$SYST/binds"
true
EOF
	} > "$W/status.sh"
	$SSH 'sh -s' < "$W/status.sh"
	exit $?
fi

if [ "$MODE" = restore ]; then
	{
		device_head
		cat <<'EOF'
cd /
b=$(busy)
case $b in *usbdrive*) echo "px2-deploy: the card is a computer's now (Flash Drive); nothing was changed" >&2; exit 4 ;; esac
if [ "$NORESTART" != 1 ] && [ -n "$b" ]; then
	printf '%s\n' "$b" | sed 's/^/busy=/' >&2
	echo "px2-deploy: busy; nothing was changed" >&2
	exit 4
fi
if [ ! -d "$SAVE" ] && [ ! -s "$SYST/binds" ]; then
	echo "px2-deploy: nothing kept, nothing to restore" >&2
	exit 3
fi
if [ -d "$SAVE" ] && [ "$FROMSYS" != 1 ] && [ "$(cat "$SAVE/system" 2> /dev/null)" != "$(sysid)" ]; then
	echo "px2-deploy: the card's files in $SAVE were kept on $(cat "$SAVE/system" 2> /dev/null | sed 's/^\(..*\)$/another system (\1)/; s/^$/a system nobody wrote down (an older tools\/px2-deploy.sh kept them)/'); this one is $(sysid): --from-system puts back this system's own; nothing was changed" >&2
	exit 7
fi
# Puts $2 (a file) at ZyperOS/$1: under a new name, then renamed
put() {
	case $1 in */*) mkdir -p "$DEST/${1%/*}" ;; esac
	cp -p "$2" "$DEST/$1.px2-new" && mv -f "$DEST/$1.px2-new" "$DEST/$1"
}
if [ -d "$SAVE" ]; then
	{ [ -d "$SAVE/orig" ] && (cd "$SAVE/orig" && find . -type f | sed 's|^\./||'); [ -f "$SAVE/added" ] && cat "$SAVE/added"; } |
	while IFS= read -r rel; do
		if [ "$FROMSYS" = 1 ] && [ -f "$SEED/ZyperOS/$rel" ]; then
			put "$rel" "$SEED/ZyperOS/$rel" || { echo "px2-deploy: could not put back $rel" >&2; exit 8; }
		elif [ -f "$SAVE/orig/$rel" ]; then
			put "$rel" "$SAVE/orig/$rel" || { echo "px2-deploy: could not put back $rel" >&2; exit 8; }
		else
			rm -f "$DEST/$rel"
			printf 'restored\t%s\t%s\n' "$rel" "removed, as before the deploy"
			continue
		fi
		printf 'restored\t%s\t%s\n' "$rel" "$(verdict "$rel")"
	done || exit $?
	sync
	rm -rf "$SAVE"
	sync
fi
if [ -s "$SYST/binds" ]; then
	while IFS="$TAB" read -r sha abs; do
		in_init umount "$ROOT$abs" && printf 'unbound\t%s\n' "$abs"
	done < "$SYST/binds"
	rm -rf "$SYST"
fi
true
EOF
	} > "$W/restore.sh"
	# The facts the restart and its check need first
	: > "$W/rels"
	: > "$W/abs"
	probe
	set +e
	$SSH 'sh -s' < "$W/restore.sh" > "$W/done"
	rc=$?
	set -e
	[ "$rc" = 0 ] || exit "$rc"
	awk -F"$TAB" '$1 == "restored" { print "  restored " $2 " (" $3 ")" } $1 == "unbound" { print "  " $2 ": the system'"'"'s own again" }' "$W/done"
	awk -F"$TAB" '$1 == "restored" || $1 == "unbound" { print $2 }' "$W/done" > "$W/changed"
	plan
	say_plan
	[ "$NORESTART" = 1 ] && exit 0
	restart
	verify
	exit $?
fi

# --- deploy -------------------------------------------------------------------

[ -d "$SRC" ] || die "no folder $SRC"
[ -f "$SRC/zyperos.elf" ] || die "no zyperos.elf in $SRC"
SRC=$(cd "$SRC" && pwd)
if [ -f "$SRC/../BUILT" ]; then
	echo "px2-deploy: $SRC, ZyperOS $(sed -n 's/^zyperos=//p' "$SRC/../BUILT") against $(sed -n 's/^sdk=//p' "$SRC/../BUILT") (plastron $(sed -n 's/^sdk.plastron=//p' "$SRC/../BUILT"))"
fi
(cd "$SRC" && find . -type f ! -name '._*' ! -name .DS_Store ! -name '*.px2-new' | sed 's|^\./||' | LC_ALL=C sort) > "$W/rels"
while IFS= read -r rel; do
	printf '%s\t%s\t%s\n' "$(sha "$SRC/$rel")" "$(flags "$SRC/$rel")" "$rel"
done < "$W/rels" > "$W/local"
system_local

probe
echo "px2-deploy: the Pixel's card has ZyperOS $(fact cardver), its system $(fact seedver)$([ "$(fact ns)" = own ] && echo ", SSH in a namespace of its own (Developer Mode)")"
[ "$(fact busy)" != usbdrive ] || die "the card is a computer's now (Flash Drive): eject it there first"
if grep -q '^sysnone' "$W/facts"; then
	awk -F"$TAB" '$1 == "sysnone" { print "px2-deploy: the system has no " $3 " to stand in for: a new file needs an image" }' "$W/facts" >&2
	exit 8
fi

# What differs from the card: its files by checksum, the system's through a
# stand-in already bound if there is one
# Files kept on another system, or by an older deploy that wrote none down:
# a restore would put them back over this one's, so nothing goes on top
if [ "$KEEP" = 1 ] && grep -q '^kept=' "$W/facts" && [ "$(fact keptsys)" != "$(fact sysid)" ]; then
	echo "px2-deploy: the card's files kept in $SAVE (since $(fact kept)) are $([ -n "$(fact keptsys)" ] && echo "another system's ($(fact keptsys))" || echo "of a system nobody wrote down (an older tools/px2-deploy.sh kept them)"); this one is $(fact sysid)" >&2
	echo "px2-deploy: tools/px2 restore --from-system first (this system's own copies back), or move $SAVE aside; nothing was sent" >&2
	exit 7
fi
awk -F"$TAB" '$1 == "sum" { print $2 "\t" $3 }' "$W/facts" > "$W/remote"
awk -F"$TAB" 'NR == FNR { have[$2] = $1; next } have[$3] != $1' "$W/remote" "$W/local" > "$W/card.list"
{
	awk -F"$TAB" '$1 == "syssum" { print $2 "\t" $3 }' "$W/facts"
	awk -F"$TAB" '$1 == "bind" { print $2 "\t" $3 }' "$W/facts"
} | awk -F"$TAB" '{ have[$2] = $1 } END { for (a in have) print have[a] "\t" a }' > "$W/sysremote"
awk -F"$TAB" 'NR == FNR { have[$2] = $1; next } have[$3] != $1' "$W/sysremote" "$W/sys.local" > "$W/sys.list"
same=$(($(wc -l < "$W/local") - $(wc -l < "$W/card.list")))
if [ ! -s "$W/card.list" ] && [ ! -s "$W/sys.list" ]; then
	echo "px2-deploy: the card has every one of these $same files already; nothing sent, nothing restarted"
	exit 0
fi
while IFS="$TAB" read -r s f rel; do
	if awk -F"$TAB" -v r="$rel" '$2 == r { f = 1 } END { exit !f }' "$W/remote"; then how=changed; else how=new; fi
	echo "  send $rel ($(size "$SRC/$rel"), $how)"
done < "$W/card.list"
while IFS="$TAB" read -r s f abs; do
	echo "  stand in for $abs ($(size "$SYSDIR$abs"), until the next restart)"
done < "$W/sys.list"
[ "$same" -gt 0 ] && echo "  ($same more the card has already)"
awk -F"$TAB" '{ print $3 }' "$W/card.list" "$W/sys.list" > "$W/changed"
plan
b=$(fact busy)
if [ -n "$b" ] && [ "$NORESTART" != 1 ]; then
	case $b in
	port) why="a port is running: end it first" ;;
	game\ *) why="a game is open (${b#game }): quit it first (tools/px2 quit)" ;;
	nolauncher) why="ZyperOS is not running (a restart, or its charging screen's power-off)" ;;
	*) why=$b ;;
	esac
	die "$why, or say --no-restart to put the files without a restart; nothing was sent"
fi

# The files, in one copy
mkdir -p "$W/stage/card" "$W/stage/sys"
while IFS="$TAB" read -r s f rel; do
	case $rel in */*) mkdir -p "$W/stage/card/${rel%/*}" ;; esac
	cp -p "$SRC/$rel" "$W/stage/card/$rel"
done < "$W/card.list"
while IFS="$TAB" read -r s f abs; do
	mkdir -p "$W/stage/sys${abs%/*}"
	cp -p "$SYSDIR$abs" "$W/stage/sys$abs"
done < "$W/sys.list"
if [ -s "$W/sys.list" ] && [ "$(fact ns)" = own ]; then
	[ -f "$INITNS" ] || die "SSH has a namespace of its own here, and there is no $INITNS to bind the system's files with: make px2-programs"
	cp -p "$INITNS" "$W/stage/initns"
fi
$SCP "$W/stage" "root@$HOST:$DTMP" < /dev/null || die "the copy to $HOST failed; nothing was changed"

# Into place, on the device
REBOOT_SYS=
if [ "$(head -1 "$W/plan")" = reboot ] && [ -s "$W/sys.list" ] && [ "$NORESTART" != 1 ]; then
	# A restart of the device takes the stand-ins away: bound once SSH is back
	REBOOT_SYS=1
	cp "$W/sys.list" "$W/sys.after"
	: > "$W/sys.list"
fi
apply_script > "$W/apply.sh"
set +e
$SSH 'sh -s' < "$W/apply.sh" > "$W/done"
rc=$?
set -e
if [ "$rc" = 6 ]; then
	sdk=$(sed -n 's/^sdk=//p' "$SRC/../BUILT" 2> /dev/null)
	echo "px2-deploy: these were built against ${sdk:-an SDK this folder does not name}$([ -f "$SRC/../BUILT" ] && echo " (plastron $(sed -n 's/^sdk.plastron=//p' "$SRC/../BUILT"))"), and the Pixel runs a system with ZyperOS $(fact seedver) that lacks the above: build them against that system's SDK (plastron's make sdk after its build, then PX2_SDK=plastron-sdk-px2-<id>), or put a system built with this SDK on the Pixel" >&2
fi
[ "$rc" = 0 ] || exit "$rc"
n=$(grep -c "^put$TAB" "$W/done" || true)
[ "$n" -gt 0 ] && echo "px2-deploy: $n file$([ "$n" = 1 ] || echo s) on the card$([ "$KEEP" = 1 ] && echo "; the card's own are kept: tools/px2 restore puts them back")"
awk -F"$TAB" '$1 == "bound" { print "px2-deploy: " $2 " stands in for the system'"'"'s own until the next restart (or tools/px2 restore)" }' "$W/done"
say_plan
[ "$NORESTART" = 1 ] && exit 0
restart
rc=0
verify || rc=$?
if [ -n "$REBOOT_SYS" ] && [ "$rc" = 0 ]; then
	# The device has started again: the system's files bound now, then what
	# reads them started again
	echo "px2-deploy: the device is back; binding the system's files again"
	cp "$W/sys.after" "$W/sys.list"
	: > "$W/card.list"
	: > "$W/rels"
	cut -f3 "$W/sys.list" > "$W/abs"
	probe
	mkdir -p "$W/stage2/card" "$W/stage2/sys"
	while IFS="$TAB" read -r s f abs; do
		mkdir -p "$W/stage2/sys${abs%/*}"
		cp -p "$SYSDIR$abs" "$W/stage2/sys$abs"
	done < "$W/sys.list"
	[ "$(fact ns)" = own ] && cp -p "$INITNS" "$W/stage2/initns"
	$SCP "$W/stage2" "root@$HOST:$DTMP" < /dev/null || die "the copy to $HOST failed"
	apply_script > "$W/apply.sh"
	$SSH 'sh -s' < "$W/apply.sh" > "$W/done" || exit $?
	awk -F"$TAB" '$1 == "bound" { print "px2-deploy: " $2 " stands in for the system'"'"'s own until the next restart (or tools/px2 restore)" }' "$W/done"
	cut -f3 "$W/sys.list" > "$W/changed"
	plan
	say_plan
	restart
	verify || rc=$?
fi
exit $rc
