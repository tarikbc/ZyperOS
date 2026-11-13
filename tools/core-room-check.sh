#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# make check-core-room: Diatom's resident registry holds DIATOM_MAX_RESIDENT
# cores (diatom/src/diatom.h), and it premaps every core on the card in folder
# order, so a card with more cores than that has the last ones refused, and
# their shelves start every game in the launcher's one-off Diatom. 1.0.5
# shipped that way: 34 cores, room for 32, Vectrex and Saturn left out.
# systems.cfg names every libretro core a card ships (mk/payload.sh refuses a
# card that lacks one), so its count is checked against the room here, and
# mk/payload.sh checks the cores it actually copies.
set -u
cd "$(dirname "$0")/.."

room=$(awk '$1 == "#define" && $2 == "DIATOM_MAX_RESIDENT" { print $3 }' diatom/src/diatom.h)
[ -n "$room" ] || { echo "  FAIL no DIATOM_MAX_RESIDENT in diatom/src/diatom.h"; exit 1; }
cores=$(awk -F'|' '$1 == "sys" { n = split($4, c, ","); for (i = 1; i <= n; i++) {
            gsub(/^[ \t]+|[ \t]+$/, "", c[i]); if (c[i] != "port" && c[i] !~ /^run:/) print c[i] } }' \
        config/systems.cfg | sort -u | wc -l | tr -d ' ')
if [ "$cores" -gt "$room" ]; then
	echo "  FAIL systems.cfg names $cores libretro cores, and Diatom keeps $room open"
	echo "       (DIATOM_MAX_RESIDENT in diatom/src/diatom.h): raise it"
	exit 1
fi
echo "  ok   systems.cfg names $cores libretro cores, and Diatom keeps up to $room open"
