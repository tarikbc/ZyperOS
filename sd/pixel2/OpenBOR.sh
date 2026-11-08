#!/bin/bash
# OpenBOR, the OpenBOR shelf's program (config/systems.cfg, mk/build-openbor.sh).
# launch.sh's run_port starts this with the game, a module's .pak, as its
# argument; named on OpenBOR's command line a module starts at once, without
# OpenBOR's own menu of modules.
#
# OpenBOR keeps each module's settings and saves in Saves/, and its Logs and
# ScreenShots, beside where it starts, and makes a Paks/ there that it would
# list: all of it in Saves/openbor on the card, with the card's other saves.
#
# The pad is SDL's: plastron's SDL2 maps the Pixel 2's pad itself (the
# standard gamepad codes, res/portmaster/control.txt), and OpenBOR lays a
# known pad out as attack on A, jump on B, the second attack on X, special on
# Y, the two others on L1 and R1, START its pause and SELECT a screenshot
# (its sdl/control.c; in QEMU its menu answers X and Y so, 2026-11-07). A game
# ends from OpenBOR's own menus, or from ZyperOS's in-game menu on MENU,
# which portkeys shows over it (launch.sh gives it --menu for this script;
# src/portmenu.h).
DIR=/mnt/SDCARD/Saves/openbor
mkdir -p "$DIR"
cd "$DIR" || exit 1
exec "${0%/*}/OpenBOR" "$1"
