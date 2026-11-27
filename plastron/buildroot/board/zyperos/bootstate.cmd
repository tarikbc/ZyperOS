# The A/B slots (ZyperOS's docs/updates.md, layer 2), the same on every
# board: each board's post-image.sh puts its own few lines in front of this
# (boot-prelude.cmd: zy_root, the root device without its number, zy_dtb,
# the device tree's file or nothing, zy_args, the kernel's other arguments)
# and builds boot.scr from the two.
#
# /bootstate.txt on this partition says which slot boots: slot=a (root in
# partition 2, kernel in /a) or slot=b (partition 4, /b). A new system starts
# on trial (trial=1): each start adds one to try and writes it back, and the
# third start that the system has not confirmed (zyperos-update confirm,
# once the launcher is up) boots the other slot again and says so (fell=).
# Writes happen only on trial; a normal start only reads. A card with no
# bootstate.txt boots slot a, as one written before the slots did. A start a
# charger made (zy_charge, below) is not counted: ZyperOS shows only its
# charging screen then, and confirms nothing, so a charger plugged in and out
# would otherwise send a new system back.
setexpr zy_state ${scriptaddr} + 0x100000
setenv slot a
setenv trial 0
setenv try 0
# Never empty: env export stops at a name that is not set. a and b are the
# system versions in each slot, for Linux; fell is the slot that failed.
setenv fell -
setenv a -
setenv b -
if load ${devtype} ${devnum}:${distro_bootpart} ${zy_state} /bootstate.txt; then
	env import -t ${zy_state} ${filesize} slot trial try fell a b
fi
setenv zy_count ${trial}
if test "${zy_charge}" = "1"; then setenv zy_count 0; fi
if test "${zy_count}" = "1"; then
	setexpr try ${try} + 1
	if itest ${try} -ge 3; then
		setenv fell ${slot}
		if test "${slot}" = "b"; then setenv slot a; else setenv slot b; fi
		setenv trial 0
		setenv try 0
	fi
	env export -t ${zy_state} slot trial try fell a b
	fatwrite ${devtype} ${devnum}:${distro_bootpart} ${zy_state} /bootstate.txt ${filesize}
fi
# The boot screen's theme, language and brightness (ZyperOS's
# src/boottheme.h, DESIGN.md 11.20 and 11.21): theme.txt, which the launcher
# keeps here, onto the kernel's line as four words without a dot, which the
# kernel hands to init's environment, where the splash reads them. "-"
# without the file, so the launcher knows this boot.scr asked and found none
# (and writes it), where an older one says nothing; the splash plays its
# default for "-", its first start's line in English, and lights the panel
# at the kernel's own level. After bootstate's write, which used the same
# memory.
setenv zytheme -
setenv zyzone -
setenv zylang -
setenv zybright -
if load ${devtype} ${devnum}:${distro_bootpart} ${zy_state} /theme.txt; then
	env import -t ${zy_state} ${filesize} zytheme zyzone zylang zybright
fi
setenv zy_theme "zytheme=${zytheme} zyzone=${zyzone} zylang=${zylang} zybright=${zybright}"
# A test of the start's watchdog, once: zytest=hang or zytest=stall in
# /zytest.txt on this partition, which a computer can write, goes onto the
# kernel's line for this start (plastron's hwdog says what each does), and
# the file is written back as zytest=- at once, so the start after is a
# normal one whatever this one does. Nothing at all without the file.
setenv zy_test
setenv zytest -
if load ${devtype} ${devnum}:${distro_bootpart} ${zy_state} /zytest.txt; then
	env import -t ${zy_state} ${filesize} zytest
	if test "${zytest}" != "-"; then
		setenv zy_test "zytest=${zytest}"
		setenv zytest -
		env export -t ${zy_state} zytest
		fatwrite ${devtype} ${devnum}:${distro_bootpart} ${zy_state} /zytest.txt ${filesize}
	fi
fi
if test "${slot}" = "b"; then setenv zy_part 4; else setenv slot a; setenv zy_part 2; fi
