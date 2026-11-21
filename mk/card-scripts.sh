#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The card's scripts for DEVICE, where its ZyperOS/ folder (P) has them:
#
#   DEVICE=pixel2 sh mk/card-scripts.sh P
#
# The one card script (sd/zyperos/launch.sh), the equalizer's config for a
# port (eq-port.sh, launch.sh's run_port), the device's own device.sh, which
# launch.sh sources; for a device with Bluetooth sound its ALSA config
# (bt-alsa.sh); and for a device that runs ports (the Pixel 2) OpenBOR's
# script, your PICO-8's and the wget its Splore downloads with
# (sd/pixel2/PICO-8-wget.sh, as pico8/bin/wget).
#
# mk/payload.sh lays the payload's out with this, and tools/px2-payload.sh
# --programs puts the same files beside the programs (make px2-programs), so
# a change to a card script goes onto a device with tools/px2 deploy, with no
# cores and no image. One list, so the two cannot drift.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEVICE=${DEVICE:-brick}
P=${1:?usage: DEVICE=<id> mk/card-scripts.sh ZYPEROS_DIR}
DEVFILE=$ROOT/config/device/$DEVICE.mk
DEVICE_SH=$ROOT/sd/$DEVICE/device.sh
[ -f "$DEVFILE" ] && [ -f "$DEVICE_SH" ] ||
	{ echo "card-scripts: unknown DEVICE $DEVICE (no config/device/$DEVICE.mk or sd/$DEVICE/device.sh)" >&2; exit 1; }
HELPERS=" $(sed -n 's/^ZYDEV_HELPERS = *//p' "$DEVFILE") "
has_helper() { case $HELPERS in *" $1 "*) return 0 ;; esac; return 1; }

mkdir -p "$P"
cp "$ROOT/sd/zyperos/launch.sh" "$P/launch.sh"
# The equalizer's config for a port (launch.sh's run_port), on every device:
# a device whose device.sh names no EQ_SLAVE never runs it
cp "$ROOT/sd/zyperos/eq-port.sh" "$P/eq-port.sh"
cp "$DEVICE_SH" "$P/device.sh"
chmod +x "$P/launch.sh"
# The headsets' ALSA config: the Brick's device.sh sources it at boot, and
# ZyperOS runs it after a pair or a forget
if has_helper btalsa; then cp "$ROOT/sd/zyperos/bt-alsa.sh" "$P/"; fi
if has_helper ports; then
	# The script OpenBOR's shelf's "run:" names, beside the program
	mkdir -p "$P/openbor"
	cp "$ROOT/sd/pixel2/OpenBOR.sh" "$P/openbor/"
	chmod +x "$P/openbor/OpenBOR.sh"
	# Your PICO-8's script, which the PICO-8 shelf's first core names
	# (config/systems.cfg), and the wget its Splore downloads with: curl,
	# with HTTPS, which the system's busybox wget lacks
	mkdir -p "$P/pico8/bin"
	cp "$ROOT/sd/pixel2/PICO-8.sh" "$P/pico8/"
	cp "$ROOT/sd/pixel2/PICO-8-wget.sh" "$P/pico8/bin/wget"
	chmod +x "$P/pico8/PICO-8.sh" "$P/pico8/bin/wget"
fi
