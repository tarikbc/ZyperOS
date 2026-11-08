#!/bin/bash
# A made-up port with no port.json: its folder is where the script works,
# its keymap gptokeyb2's, its README a plain text one.
controlfolder="/roms/ports/PortMaster"
source $controlfolder/control.txt
get_controls

PORTDIR="/$directory/ports"
GAMEDIR="$PORTDIR/lonekey"
cd $GAMEDIR

$GPTOKEYB2 "lonekey" -c "./lonekey.gptk" &
./lonekey
pm_finish
