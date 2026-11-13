#!/bin/sh
# Put a built ZyperOS payload on a GKD Pixel 2 over the USB cable, without
# taking the card out: the launcher, Diatom, launch.sh, the cores, cards and
# config, whatever differs from what the card has. Then ZyperOS restarts.
#
#   tools/px2-deploy.sh PAYLOAD_DIR      (the ZyperOS/ folder of a payload, as
#                                         mk/payload.sh lays it out, or
#                                         plastron's /usr/share/zyperos/card/ZyperOS)
#
# Needs a development build of plastron (its SSH key on the card) and the
# device at 10.42.0.1 (PX2_HOST), on SSH's port 22 (PX2_PORT: a QEMU VM's
# forwarded port, tools/qemu-run.sh). Each file goes to /tmp on the device first
# and is moved into place only once its checksum matches, so a cable pulled
# halfway leaves the old file, not half a new one. plastron's own
# scripts/px2-push.sh is the same thing for the system partition.
set -eu

SRC=${1:?usage: px2-deploy.sh PAYLOAD_DIR}
HOST=${PX2_HOST:-10.42.0.1}
PORT=${PX2_PORT:-22}
KEY=${PX2_KEY:-$HOME/.ssh/zyperos_pixel2}
DEST=/mnt/SDCARD/ZyperOS
# dropbear makes a new host key at every boot of a read-only root: no key to check
OPTS="-o ConnectTimeout=8 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
SSH="ssh $OPTS -p $PORT -i $KEY root@$HOST"
SCP="scp -O -q $OPTS -P $PORT -i $KEY"

[ -f "$SRC/zyperos.elf" ] || { echo "px2-deploy: no zyperos.elf in $SRC" >&2; exit 1; }

# What the card has, by checksum, in one round trip
REMOTE=$($SSH "cd $DEST && find . -type f -exec sha256sum {} +" || true)

cd "$SRC"
LIST=$(mktemp)
trap 'rm -f "$LIST"' EXIT
find . -type f ! -name '._*' ! -name .DS_Store | sort | while read -r f; do
	want=$(shasum -a 256 "$f" | cut -d' ' -f1)
	# The name is everything after the checksum's two spaces: a file whose
	# name has a space in it (a menu music track's) matched by $2 never would
	have=$(printf '%s\n' "$REMOTE" | awk -v f="$f" '{ h = $1; sub(/^[^ ]+  /, ""); if ($0 == f) print h }')
	[ "$want" = "$have" ] && continue
	rel=${f#./}
	$SSH "mkdir -p /tmp/px2-deploy" < /dev/null
	$SCP "$f" "root@$HOST:/tmp/px2-deploy/file" < /dev/null
	$SSH "set -e; got=\$(sha256sum /tmp/px2-deploy/file | cut -d' ' -f1)
		[ \"\$got\" = $want ] || { echo 'px2-deploy: $rel arrived damaged' >&2; exit 1; }
		mkdir -p \"$DEST/\$(dirname '$rel')\"
		mv -f /tmp/px2-deploy/file '$DEST/$rel'
		case '$rel' in *.sh|zyperos.elf|diatom|muse|btplayer|p2d|openbor/OpenBOR) chmod +x '$DEST/$rel' ;; esac" < /dev/null
	echo "  sent $rel"
	echo "$rel" >> "$LIST"
done
$SSH "sync; rm -rf /tmp/px2-deploy"

# A new launch.sh only runs from the next boot: the shell running the old one
# keeps reading the file it opened. Anything else: the launcher restarts
# itself when it exits (launch.sh), and Diatom with it, so a new diatom or
# core is the one that runs.
if grep -qx launch.sh "$LIST"; then
	$SSH 'reboot' || true
	echo "px2-deploy: done; launch.sh changed, so the device is restarting"
else
	$SSH 'kill $(pidof diatom) 2> /dev/null; kill $(pidof zyperos.elf) 2> /dev/null; true'
	echo "px2-deploy: done; ZyperOS is restarting"
fi
