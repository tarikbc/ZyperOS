################################################################################
#
# zyperos: the launcher and Muse, from the ZyperOS repository
#
################################################################################

ZYPEROS_VERSION = local
ZYPEROS_SITE = /zyperos
ZYPEROS_SITE_METHOD = local
# diatom too: its binary goes on the card with ZyperOS's (see the install)
ZYPEROS_DEPENDENCIES = sdl2 sdl2_image sdl2_ttf ffmpeg sqlite alsa-lib zlib diatom
# System Update's program (zyperos-update): its download, its signature check
ZYPEROS_DEPENDENCIES += libcurl openssl
# libdbus for btplayer, built only for the Brick (below)
ZYPEROS_DEPENDENCIES += $(if $(BR2_PACKAGE_PVR_GE8300),dbus)
ZYPEROS_LICENSE = GPL-3.0+
ZYPEROS_LICENSE_FILES = LICENSE THIRD-PARTY-LICENSES.md
# Its source is public (github.com/tarikbc/ZyperOS), so legal-info names
# it rather than packing up the copy below, which is a working tree's
ZYPEROS_REDISTRIBUTE = NO

# The source is copied from a working tree, so what git ignores stays behind:
# build products (a Brick build among them), vendor/, the ScreenScraper pair's
# file, notes. Then the worktrees, and AUDIOBOOKS/, staging material that is
# untracked but not ignored. And plastron/, this system itself, which shares
# the repository: copied along, any change to the system would rebuild the
# launcher.
ZYPEROS_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS = \
	--filter=:-_.gitignore --exclude=/.claude --exclude=/AUDIOBOOKS --exclude=/plastron

# ZyperOS's own cross build (mk/cross.mk), pointed at this system's compiler
# and libraries, with the device's own file (src/device/, picked below). The
# generated ScreenScraper header first, on its own: the elf needs it and has
# no rule to make it.
#
# Into build-<device>/, never build/: the source used to be copied in whole,
# ignored files and all, so a Brick build sitting in the worktree's build/
# came along and make took its muse as up to date. The Brick's binary shipped
# as the Pixel's, caught 2026-10-01 by its size. The copy leaves build/ behind
# now (above), and a directory of its own makes sure of it.
ZYPEROS_BUILD_DIR = build-$(ZYPEROS_DEVICE)
#
# The Brick's system is the one with the Brick's GPU libraries: ZyperOS's
# Brick device file (src/device/brick.c) then, into build-brick/.
ZYPEROS_DEVICE = $(if $(BR2_PACKAGE_PVR_GE8300),brick,pixel2)
#
# The ScreenScraper pair comes from the file the Makefile mounts, read here
# in the shell so neither value is written into a command line make prints.
ZYPEROS_SS_ENV = /zyperos-ss.env
#
# The version is ZyperOS's own, from its Makefile's VERSION line: cross.mk
# alone says 0.0, and the Pixel's About page did until 2026-10-03.
ZYPEROS_VERSION_NUMBER = $$(sed -n 's/^VERSION ?= //p' $(@D)/Makefile)
define ZYPEROS_BUILD_CMDS
	SS_DEVID="$$(sed -n 's/^SS_DEVID=//p' $(ZYPEROS_SS_ENV) 2>/dev/null | head -1)" \
	SS_DEVPASS="$$(sed -n 's/^SS_DEVPASS=//p' $(ZYPEROS_SS_ENV) 2>/dev/null | head -1)" \
		$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) -f mk/cross.mk BUILD=$(ZYPEROS_BUILD_DIR) creds
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) -f mk/cross.mk \
		CC="$(TARGET_CC)" SYSROOT="$(STAGING_DIR)" DEVICE=$(ZYPEROS_DEVICE) \
		BUILD=$(ZYPEROS_BUILD_DIR) VERSION="$(ZYPEROS_VERSION_NUMBER)" \
		$(ZYPEROS_BUILD_DIR)/zyperos.elf $(ZYPEROS_BUILD_DIR)/muse $(ZYPEROS_EXTRA)
endef
#
# And on the Brick, the one with Bluetooth, btplayer (tools/btplayer.c): a
# headset's play, pause, next and previous for Muse, and its own volume,
# run from the card by /etc/init.d/bluetooth
ZYPEROS_EXTRA = $(if $(filter brick,$(ZYPEROS_DEVICE)),$(ZYPEROS_BUILD_DIR)/btplayer,$(ZYPEROS_BUILD_DIR)/portkeys)
# and on the Pixel 2, System Update (ZyperOS's update/, docs/updates.md)
ZYPEROS_EXTRA += $(if $(filter pixel2,$(ZYPEROS_DEVICE)),$(ZYPEROS_BUILD_DIR)/zyperos-update)

# ZyperOS runs from the card's games partition, as on the Brick, so it can be
# updated by copying files onto the card. What goes there is ZyperOS's own
# payload (mk/payload.sh, DEVICE=pixel2): the launcher, Muse, diatom, the
# cores, cards and fonts, and the empty Roms, Bios and Saves folders. The
# image keeps a copy in /usr/share/zyperos/card, laid out as on the card, and
# /etc/init.d/zyperos copies it there when the card has no ZyperOS, or an older
# one: the version goes in as ZyperOS/VERSION, which the payload writes only
# when it is given one (it was never passed before 2026-10-06).
ZYPEROS_CARD = $(TARGET_DIR)/usr/share/zyperos/card
ifeq ($(ZYPEROS_DEVICE),brick)
# The Brick's: ZyperOS's payload for it, which /etc/init.d/zyperos copies onto
# the card
define ZYPEROS_INSTALL_TARGET_CMDS
	DEVICE=brick OUT=$(ZYPEROS_CARD) VERSION="$(ZYPEROS_VERSION_NUMBER)" \
		ZYPEROS_ELF=$(@D)/$(ZYPEROS_BUILD_DIR)/zyperos.elf \
		MUSE_ELF=$(@D)/$(ZYPEROS_BUILD_DIR)/muse \
		BTPLAYER_ELF=$(@D)/$(ZYPEROS_BUILD_DIR)/btplayer \
		DIATOM_ELF=$(DIATOM_DIR)/build/brick/diatom \
		VENDOR_DIR=/zyperos-vendor \
		sh $(@D)/mk/payload.sh
endef
else
define ZYPEROS_INSTALL_TARGET_CMDS
	DEVICE=pixel2 OUT=$(ZYPEROS_CARD) VERSION="$(ZYPEROS_VERSION_NUMBER)" \
		ZYPEROS_ELF=$(@D)/$(ZYPEROS_BUILD_DIR)/zyperos.elf \
		MUSE_ELF=$(@D)/$(ZYPEROS_BUILD_DIR)/muse \
		PORTKEYS_ELF=$(@D)/$(ZYPEROS_BUILD_DIR)/portkeys \
		UPDATE_ELF=$(@D)/$(ZYPEROS_BUILD_DIR)/zyperos-update \
		DIATOM_ELF=$(DIATOM_DIR)/build/pixel2/diatom \
		VENDOR_DIR=/zyperos-vendor \
		sh $(@D)/mk/payload.sh
	$(ZYPEROS_INSTALL_UPDATE)
endef
endif

# System Update (ZyperOS's update/ and docs/updates.md). The image holds what
# the card must not be able to change: the public keys a release list must be
# signed with (ZyperOS's res/update/keys), the switch that runs at every start
# before anything from the card (update/apply.sh, called by zyperos.init), and
# which system this is, for a release that needs a newer one. An image built
# from a ZyperOS with no keys has System Update switched off: the program
# says to use the installer.
define ZYPEROS_INSTALL_UPDATE
	rm -rf $(TARGET_DIR)/etc/zyperos/update-keys
	mkdir -p $(TARGET_DIR)/etc/zyperos/update-keys
	for k in $(@D)/res/update/keys/*.pem; do \
		[ -f "$$k" ] || continue; \
		$(INSTALL) -m 0644 "$$k" $(TARGET_DIR)/etc/zyperos/update-keys/ || exit 1; \
	done
	printf 'version %s\nupdate 1\ndevice %s\n' "$(ZYPEROS_VERSION_NUMBER)" "$(ZYPEROS_DEVICE)" \
		> $(TARGET_DIR)/etc/zyperos/system
	$(INSTALL) -D -m 0755 $(@D)/update/apply.sh $(TARGET_DIR)/usr/lib/zyperos/update-apply.sh
endef

# Every image carries ZyperOS's px2-shot, px2-input, px2-perf and px2-snap
# in /usr/bin, so a screenshot, a button press, a performance report and a
# game's saves set aside are one SSH command away: in QEMU
# (tools/qemu-run.sh), on a development image, and on a release image with
# Developer Mode on. They are small, need no library (px2-snap is a shell
# script), and only root can use them (the framebuffer's GETFB2 and uinput,
# the card). tools/px2 on a computer drives them (docs/building.md).
ZYPEROS_SSH_TOOLS = px2-shot px2-input px2-perf
define ZYPEROS_BUILD_SSH_TOOLS
	for t in $(ZYPEROS_SSH_TOOLS); do \
		$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -Wall -Wextra \
			-o $(@D)/$(ZYPEROS_BUILD_DIR)/$$t $(@D)/tools/$$t.c -lm || exit 1; \
	done
endef
ZYPEROS_POST_BUILD_HOOKS += ZYPEROS_BUILD_SSH_TOOLS
define ZYPEROS_INSTALL_SSH_TOOLS
	for t in $(ZYPEROS_SSH_TOOLS); do \
		$(INSTALL) -D -m 0755 $(@D)/$(ZYPEROS_BUILD_DIR)/$$t $(TARGET_DIR)/usr/bin/$$t || exit 1; \
	done
	$(INSTALL) -D -m 0755 $(@D)/tools/px2-snap.sh $(TARGET_DIR)/usr/bin/px2-snap
endef
ZYPEROS_POST_INSTALL_TARGET_HOOKS += ZYPEROS_INSTALL_SSH_TOOLS

# Its start and stop, the same on both devices: copied onto the card from the
# image when the card has no ZyperOS or an older one, launch.sh run, and at
# shutdown ZyperOS stopped ahead of everything else (zyperos.init). And the
# end of a power-off it asked for (zyperos-lastoff, the inittab's last
# shutdown line)
define ZYPEROS_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(ZYPEROS_PKGDIR)/zyperos.init $(TARGET_DIR)/etc/init.d/zyperos
	$(INSTALL) -D -m 0755 $(ZYPEROS_PKGDIR)/zyperos-lastoff $(TARGET_DIR)/usr/lib/zyperos/lastoff
endef

$(eval $(generic-package))
