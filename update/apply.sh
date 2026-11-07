#!/bin/sh
# System Update's switch, run at every start by /etc/init.d/zyperos before
# anything from the card runs (plastron installs this as
# /usr/lib/zyperos/update-apply.sh):
#
#   update-apply.sh CARD [DEVICE]
#
# DEVICE (pixel2, brick) names the card's .userdata folder; without it, the
# system file's "device" line says it.
# It lives in the system image, not on the card: the card's ZyperOS folder is
# what it swaps, launch.sh included. Every step is a rename, so a power cut
# at any point leaves a state the next start finishes:
#
#   ZyperOS/        the version that runs
#   ZyperOS.next/   a staged update; .ready in it (written last, after every
#                   file was checked) means it is complete
#   ZyperOS.prev/   the version before, for one step back
#   ZyperOS.swap/   only between the three renames of a step back
#   ZyperOS.bad/    a version that did not start, on its way out
#
# A new version starts on trial: ZyperOS/.trial counts the starts it has had
# without the launcher saying it is up (it deletes the file once its shelf
# has been up for a while). At two, the next start goes back to the previous
# version by itself and says so in update/result.
#
# Never a step back to a version older than this image's own ZyperOS: the
# init script would copy the image's straight over it.
CARD=${1:?card}
DEVICE=${2:-}
SYSTEM=${ZYPEROS_UPDATE_SYSTEM:-/etc/zyperos/system}
# The kernel's line, or a file standing for it in tests (as update/slots.c)
CMDLINE=${ZYPEROS_UPDATE_CMDLINE:-/proc/cmdline}
if [ -z "$DEVICE" ] && [ -f "$SYSTEM" ]; then
	while read -r k val; do [ "$k" = device ] && DEVICE=$val; done < "$SYSTEM"
fi
DEVICE=${DEVICE:-pixel2}
LIVE=$CARD/ZyperOS
NEXT=$CARD/ZyperOS.next
PREV=$CARD/ZyperOS.prev
SWAP=$CARD/ZyperOS.swap
BAD=$CARD/ZyperOS.bad
STATE=$CARD/.userdata/$DEVICE/update

say() {
	echo "zyperos-update: $*" > /dev/kmsg 2> /dev/null || echo "zyperos-update: $*" >&2
	[ -d "$STATE" ] || mkdir -p "$STATE"
	echo "$(date -u '+%Y-%m-%dT%H:%M:%SZ') apply: $*" >> "$STATE/log"
}
# What this start did, for System Update's page; and the program's last
# status goes, as it was about a folder that has changed under it (a "ready"
# or a "rollback" from before the restart). The page reads none as idle and
# looks again.
result() {
	[ -d "$STATE" ] || mkdir -p "$STATE"
	echo "$*" > "$STATE/result"
	rm -f "$STATE/status"
}

# 1.4.0 as 1004000 in $v, false when it is not a version; version_of reads
# it from a file
version_num() {
	local s a b c
	s=${1%%-*}
	a=${s%%.*}; s=${s#*.}
	b=${s%%.*}; c=${s#*.}
	case $a$b$c in *[!0-9]*|'') return 1 ;; esac
	v=$((a * 1000000 + b * 1000 + c))
}
version_of() {
	local s
	[ -f "$1" ] && read -r s < "$1" && version_num "$s"
}

SYS_V=0
if [ -f "$SYSTEM" ]; then
	while read -r k val; do
		[ "$k" = version ] && version_num "$val" && SYS_V=$v
	done < "$SYSTEM"
fi

# A version that may be gone back to: has a VERSION, not older than the image's
prev_ok() {
	version_of "$PREV/VERSION" && [ "$v" -ge "$SYS_V" ]
}

# The launcher's own state in its folder (where it was, what was playing)
# goes along: every top-level dotfile of the old folder that the new one has
# not, except the update's own
carry_state() {
	local f n
	for f in "$1"/.[!.]*; do
		[ -f "$f" ] || continue
		n=${f##*/}
		case $n in .files|.update|.ready|.trial|.staged) continue ;; esac
		[ -e "$2/$n" ] || cp -p "$f" "$2/$n"
	done
}

# Files the update adds outside the folder (Licenses/, empty Roms folders):
# added to the card, never taken away, as the image's first copy does
card_extras() {
	[ -d "$LIVE/.card" ] || return 0
	cp -R "$LIVE/.card/." "$CARD/"
}

# 1. A switch cut short: put a folder back where the init script looks
if [ ! -d "$LIVE" ]; then
	if [ -f "$NEXT/.ready" ]; then
		mv "$NEXT" "$LIVE" && rm -f "$LIVE/.ready" && echo 0 > "$LIVE/.trial"
		say "finished a cut-short switch to $(cat "$LIVE/VERSION" 2>/dev/null)"
		result "updated $(cat "$PREV/VERSION" 2>/dev/null) $(cat "$LIVE/VERSION" 2>/dev/null)"
	elif [ -d "$PREV" ]; then
		mv "$PREV" "$LIVE" && say "put the previous version back after a cut-short switch"
	elif [ -d "$SWAP" ]; then
		mv "$SWAP" "$LIVE" && say "put the swapped-out version back"
	fi
fi
if [ -d "$SWAP" ]; then
	if [ ! -d "$PREV" ]; then mv "$SWAP" "$PREV"; else rm -rf "$SWAP"; fi
fi
[ -d "$BAD" ] && rm -rf "$BAD"

# 2. A step back the player asked for (System Update > Roll Back): a swap,
# so the version left is the one a step forward goes to
if [ -f "$STATE/rollback" ]; then
	rm -f "$STATE/rollback"
	if [ -d "$LIVE" ] && [ -d "$PREV" ] && prev_ok; then
		from=$(cat "$LIVE/VERSION" 2>/dev/null)
		carry_state "$LIVE" "$PREV"
		mv "$LIVE" "$SWAP" && mv "$PREV" "$LIVE" && mv "$SWAP" "$PREV"
		rm -f "$LIVE/.trial" "$PREV/.trial"
		sync
		say "went back from $from to $(cat "$LIVE/VERSION" 2>/dev/null)"
		result "rolled-back $from"
	else
		say "a step back was asked for, but there is no version to go back to"
	fi
fi

# 3. A staged update, complete and checked: in it goes
if [ -f "$NEXT/.ready" ] && [ -d "$LIVE" ]; then
	read -r want < "$NEXT/.ready"
	have=$(cat "$NEXT/VERSION" 2>/dev/null)
	if [ "$want" != "$have" ]; then
		say "the staged update says $want but holds $have: removed"
		rm -rf "$NEXT"
	else
		from=$(cat "$LIVE/VERSION" 2>/dev/null)
		carry_state "$LIVE" "$NEXT"
		rm -rf "$PREV"
		echo 0 > "$NEXT/.trial"
		sync
		# .ready stays in ZyperOS.next/ until it is ZyperOS/: cut short
		# between the two renames, step 1 finishes the job next start
		mv "$LIVE" "$PREV" && mv "$NEXT" "$LIVE" && rm -f "$LIVE/.ready"
		card_extras
		sync
		say "updated from $from to $want"
		result "updated $from $want"
	fi
fi

# A start a charger made (zycharge=1, ZyperOS's DESIGN.md 11.25): ZyperOS
# shows only its charging screen then and says no word, so it is no start of
# the version's to count. Builtins only.
charger_start() {
	cl=
	[ -r "$CMDLINE" ] || return 1
	read -r cl < "$CMDLINE" || [ -n "$cl" ] || return 1
	case " $cl " in *" zycharge=1 "*) return 0 ;; esac
	return 1
}

# 4. A version on trial: one more start without the launcher's word, but for a
# charger's, which a charger plugged in and out would otherwise turn into a
# step back
if [ -f "$LIVE/.trial" ] && ! charger_start; then
	read -r n < "$LIVE/.trial"
	case $n in *[!0-9]*|'') n=0 ;; esac
	if [ "$n" -ge 2 ] && [ -d "$PREV" ] && prev_ok; then
		bad=$(cat "$LIVE/VERSION" 2>/dev/null)
		mv "$LIVE" "$BAD" && mv "$PREV" "$LIVE"
		rm -f "$LIVE/.trial"
		rm -rf "$BAD"
		sync
		say "$bad did not start twice: went back to $(cat "$LIVE/VERSION" 2>/dev/null)"
		result "failed $bad"
	elif [ "$n" -ge 2 ]; then
		# Nothing to go back to: keep trying it, and stop counting
		rm -f "$LIVE/.trial"
	else
		echo $((n + 1)) > "$LIVE/.trial"
	fi
fi
exit 0
