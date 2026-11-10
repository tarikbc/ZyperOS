# px2-push.sh's slot choice and bootstate.txt edits, POSIX sh with nothing
# but sed, so the same text runs on the Mac (scripts/px2-slot-check.sh) and
# on the device (px2-push.sh sends it ahead of its own script).
#
# A card with system slots (ZyperOS's docs/updates.md, layer 2) has root A
# in partition 2 and root B in partition 4, each slot's kernel and device
# tree in /a and /b of the boot partition, and /bootstate.txt there, which
# U-Boot reads (board/zyperos/boot-slots.cmd). A push writes the slot that
# is NOT running, as System Update does, and never the one the device runs
# from: on 2026-11-10 a push over the running root A killed the shell that
# was writing it (Bus error, its pages read from the squashfs being
# overwritten), before the read-back and the slot step.

# px2_slot_plan CMDLINE HAS_P4 HAS_BOOTSTATE
#   The kernel's command line, whether the card has partition 4 (1 or 0),
#   whether the boot partition has bootstate.txt (1 or 0). Prints
#   "RUNNING TARGET PARTITION" (a b mmcblk0p4, or b a mmcblk0p2), or says
#   why not on stderr and returns 1: no slots on this card, a slot the
#   kernel does not name, or a root that is not the running slot's.
px2_slot_plan() {
	_cmd=" $1 "
	_run= _root=
	case "$_cmd" in
	*" zyperos.slot=a "*) _run=a ;;
	*" zyperos.slot=b "*) _run=b ;;
	*)
		echo "px2-push: the kernel names no system slot (zyperos.slot=a or b): this card has one system; write it with the ZyperOS Installer" >&2
		return 1 ;;
	esac
	[ "$2" = 1 ] && [ "$3" = 1 ] || {
		echo "px2-push: no second system slot (partition 4 and bootstate.txt): write this card with the ZyperOS Installer" >&2
		return 1
	}
	case "$_cmd" in
	*" root=/dev/mmcblk0p2 "*) _root=mmcblk0p2 ;;
	*" root=/dev/mmcblk0p4 "*) _root=mmcblk0p4 ;;
	*)
		echo "px2-push: the root is neither slot's partition: nothing written" >&2
		return 1 ;;
	esac
	if [ "$_run" = a ]; then
		[ "$_root" = mmcblk0p2 ] || { echo "px2-push: slot a runs from $_root: nothing written" >&2; return 1; }
		echo "a b mmcblk0p4"
	else
		[ "$_root" = mmcblk0p4 ] || { echo "px2-push: slot b runs from $_root: nothing written" >&2; return 1; }
		echo "b a mmcblk0p2"
	fi
}

# px2_bootstate_set FILE KEY VALUE: KEY=VALUE in FILE, added when missing.
# Through a second file rather than sed -i, which BSD sed reads otherwise.
# VALUE is a version or a word: letters, digits, dots, plus and minus.
px2_bootstate_set() {
	case "$3" in
	"" | *[!0-9A-Za-z.+-]*) echo "px2-push: \"$3\" is not a bootstate value" >&2; return 1 ;;
	esac
	if grep -q "^$2=" "$1"; then
		sed -e "s/^$2=.*/$2=$3/" "$1" > "$1.edit" && mv "$1.edit" "$1"
	else
		echo "$2=$3" >> "$1"
	fi
}

# px2_bootstate_before FILE RUNNING TARGET
#   Before the target slot is written: the running slot boots, off trial,
#   and the target holds no system (a cut write leaves a slot nothing boots
#   or rolls back to), as System Update's install_system does
px2_bootstate_before() {
	px2_bootstate_set "$1" slot "$2" &&
	px2_bootstate_set "$1" trial 0 &&
	px2_bootstate_set "$1" try 0 &&
	px2_bootstate_set "$1" "$3" -
}

# px2_bootstate_after FILE TARGET VERSION
#   The target slot written and read back: it boots next, on trial, so a
#   system that does not come up goes back to the running one after three
#   starts (boot-slots.cmd), and the launcher's confirm keeps it
px2_bootstate_after() {
	px2_bootstate_set "$1" slot "$2" &&
	px2_bootstate_set "$1" trial 1 &&
	px2_bootstate_set "$1" try 0 &&
	px2_bootstate_set "$1" fell - &&
	px2_bootstate_set "$1" "$2" "$3"
}
