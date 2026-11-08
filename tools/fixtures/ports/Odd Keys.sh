#!/bin/bash
# A made-up port whose buttons type single arrow keys and a middle click, and
# whose README names the d-pad's ways one at a time: the words a page says
# for those, in every language (tools/i18n-shots.py).
controlfolder="/roms/ports/PortMaster"
source $controlfolder/control.txt
get_controls

GAMEDIR="/$directory/ports/oddkeys"
cd "$GAMEDIR"

$GPTOKEYB "oddkeys" -c "$GAMEDIR/oddkeys.gptk" &
./oddkeys
pm_finish
