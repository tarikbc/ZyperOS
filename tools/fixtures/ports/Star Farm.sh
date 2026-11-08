#!/bin/bash
# PORTMASTER: starfarm.zip, Star Farm.sh
# A made-up port in PortMaster's layout, for tools/portdoc-check.c and the
# Controls shots: the script, its folder, port.json, a keymap and a README.

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/starfarm"
cd "$GAMEDIR"

$GPTOKEYB "starfarm" -c "$GAMEDIR/starfarm.gptk" &
./starfarm

$ESUDO kill -9 $(pidof gptokeyb)
pm_finish
