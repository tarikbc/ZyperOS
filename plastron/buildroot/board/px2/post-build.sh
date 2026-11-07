#!/bin/sh
# Install the developer's SSH public key, if local/authorized_keys exists.
# local/ is gitignored, so the key never lands in the repository. A release
# build (PLASTRON_RELEASE=1, set by scripts/release.sh) takes it out instead:
# the key would let its owner into every card written from the image, and
# without one nobody can log in, as root has no password and dropbear turns
# down an empty one.
set -eu
KEYS=$BR2_EXTERNAL_PX2_PATH/../local/authorized_keys

if [ "${PLASTRON_RELEASE:-0}" = 1 ]; then
	rm -rf "$TARGET_DIR/root/.ssh"
	# And dropbear never starts at boot, as nobody could log in to it. The
	# program stays, as the package list says: ZyperOS's USB File Transfer
	# runs it while that screen is open, on the cable's address alone, with
	# the screen's PIN as root's password in a mount namespace of its own
	# (ZyperOS src/sshlink.h). Nothing else runs it.
	rm -f "$TARGET_DIR/etc/init.d/S50dropbear"
elif [ -f "$KEYS" ]; then
	install -d -m 700 "$TARGET_DIR/root/.ssh"
	install -m 600 "$KEYS" "$TARGET_DIR/root/.ssh/authorized_keys"
fi

# ZyperOS's version, as a number, into its init script, so a boot compares the
# card's against it without reading the image's VERSION file. Worked out by
# the init script's own version_of, so the two can't disagree.
INIT=$TARGET_DIR/etc/init.d/zyperos
SEED_VERSION=$TARGET_DIR/usr/share/zyperos/card/ZyperOS/VERSION
if [ -f "$INIT" ] && [ -f "$SEED_VERSION" ]; then
	eval "$(sed -n '/^version_of()/,/^}/p' "$INIT")"
	version_of "$SEED_VERSION" && sed -i "s/^SEED_V=.*/SEED_V=$v/" "$INIT"
	grep "^SEED_V=" "$INIT"
fi

# Finder leaves .DS_Store files in the overlay folders on a Mac
find "$TARGET_DIR" -name .DS_Store -delete

# The games partition's mount point, made here rather than at boot, as on the
# Brick: on a squashfs root nothing can be made at boot, and datapart's mkdir
# would fail, leaving the card unmounted and ZyperOS with nowhere to start from
mkdir -p "$TARGET_DIR/mnt/SDCARD"

# PortMaster's LOVE 11.5 runtime was linked against libtheora 1.1, whose
# decoder is libtheoradec.so.1. Buildroot's libtheora is 1.2.0, which renamed
# it libtheoradec.so.2 because its implementation changed, not its interface:
# every symbol keeps its version name, libtheoradec_1.0, which is what
# liblove-11.5.so asks for. So the old name points at the new library.
if [ -e "$TARGET_DIR/usr/lib/libtheoradec.so.2" ]; then
	ln -sf libtheoradec.so.2 "$TARGET_DIR/usr/lib/libtheoradec.so.1"
fi

# The licenses, on the card where a player can find them: a Licenses folder
# beside ZyperOS's, which the first boot and every ZyperOS update copy onto the
# card with it. plastron's notice and this image's package list, Diatom's
# notice, and ZyperOS's
SEED_CARD=$TARGET_DIR/usr/share/zyperos/card
if [ -d "$SEED_CARD" ]; then
	L=$SEED_CARD/Licenses
	mkdir -p "$L"
	cp "$BR2_EXTERNAL_PX2_PATH/../THIRD-PARTY.md" "$L/plastron.md"
	cp "$BR2_EXTERNAL_PX2_PATH/board/px2/third-party-packages.md" "$L/plastron-packages.md"
	[ -f /zyperos/diatom/THIRD-PARTY.md ] && cp /zyperos/diatom/THIRD-PARTY.md "$L/Diatom.md"
	[ -f "$SEED_CARD/ZyperOS/THIRD-PARTY-LICENSES.md" ] && cp "$SEED_CARD/ZyperOS/THIRD-PARTY-LICENSES.md" "$L/ZyperOS.md"
	ls "$L"
fi

# System Update's index of the image's ZyperOS folder (ZyperOS's
# docs/updates.md): every file by SHA-256 and size, which the first update
# compares against to download only what changed. Written again here, after
# Buildroot has stripped the programs and the cores: the index the package
# wrote when it installed them (ZyperOS's mk/payload.sh) is out of date by
# then, and an update read against it took every core for changed (found on
# the device, 2026-11-05: 31 files and 65 MB for a 25 MB update).
SEED_Z=$SEED_CARD/ZyperOS
if [ -f "$SEED_Z/.files" ]; then
	( cd "$SEED_Z" && find . -type f ! -path ./.files | sed 's|^\./||' | LC_ALL=C sort |
	  while IFS= read -r f; do
		printf '%s %s %s\n' "$(sha256sum "$f" | cut -c1-64)" "$(wc -c < "$f" | tr -d ' ')" "$f"
	  done ) > "$SEED_Z/.files.new"
	mv "$SEED_Z/.files.new" "$SEED_Z/.files"
	echo "post-build: $(wc -l < "$SEED_Z/.files") files in ZyperOS/.files"
fi
