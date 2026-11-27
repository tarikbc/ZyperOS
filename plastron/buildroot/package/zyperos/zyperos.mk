################################################################################
#
# zyperos: the launcher and MP4, from the ZyperOS repository
#
################################################################################

ZYPEROS_VERSION = local
ZYPEROS_SITE = /zyperos
ZYPEROS_SITE_METHOD = local
# diatom too: its binary goes on the card with ZyperOS's (see the install)
ZYPEROS_DEPENDENCIES = sdl2 sdl2_image sdl2_ttf ffmpeg sqlite alsa-lib zlib xz diatom
# System Update's program (zyperos-update): its download, its signature check
ZYPEROS_DEPENDENCIES += libcurl openssl
# libdbus for btplayer, where the device's payload carries it (below)
ZYPEROS_DEPENDENCIES += $(if $(filter btplayer,$(ZYDEV_HELPERS)),dbus)
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
# came along and make took its mp4d as up to date. The Brick's binary shipped
# as the Pixel's, caught 2026-10-01 by its size. The copy leaves build/ behind
# now (above), and a directory of its own makes sure of it.
ZYPEROS_BUILD_DIR = build-$(ZYPEROS_DEVICE)
#
# The device this system is for (BR2_PLASTRON_DEVICE, external.mk): ZyperOS's
# device file for it (src/device/<id>.c), into build-<id>/
ZYPEROS_DEVICE = $(PLASTRON_DEVICE)
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
		device-programs
endef
#
# device-programs is the launcher, MP4 and the device's helpers
# (mk/cross.mk, config/device/<id>.mk's ZYDEV_HELPERS): on the Brick btplayer
# (tools/btplayer.c, a headset's keys and volume for MP4); on the Pixel 2
# portkeys, System Update (update/), zyhome (MENU+START home), p2d (NES
# Player 2's server, src/p2d/; zlib, which it links, is in the dependencies
# above) and p8fx.so (your PICO-8's Shader and Border, src/p8fx/, a library
# its script loads before SDL). Each helper's program goes into the payload
# the same way:
ZYPEROS_HELPER_ENV_portkeys = PORTKEYS_ELF=$(@D)/$(ZYPEROS_BUILD_DIR)/portkeys
ZYPEROS_HELPER_ENV_zyhome = ZYHOME_ELF=$(@D)/$(ZYPEROS_BUILD_DIR)/zyhome
ZYPEROS_HELPER_ENV_update = UPDATE_ELF=$(@D)/$(ZYPEROS_BUILD_DIR)/zyperos-update
ZYPEROS_HELPER_ENV_p2d = P2D_ELF=$(@D)/$(ZYPEROS_BUILD_DIR)/p2d
ZYPEROS_HELPER_ENV_btplayer = BTPLAYER_ELF=$(@D)/$(ZYPEROS_BUILD_DIR)/btplayer
ZYPEROS_HELPER_ENV_p8fx = P8FX_SO=$(@D)/$(ZYPEROS_BUILD_DIR)/p8fx.so

# ZyperOS runs from the card's games partition, as on the Brick, so it can be
# updated by copying files onto the card. What goes there is ZyperOS's own
# payload (mk/payload.sh, for the device): the launcher, MP4, diatom, the
# cores, cards and fonts, and the empty Roms, Music, Audiobooks, Bios, Saves
# and Menu Music folders. The
# image keeps a copy in /usr/share/zyperos/card, laid out as on the card, and
# /etc/init.d/zyperos copies it there when the card has no ZyperOS, or an older
# one: the version goes in as ZyperOS/VERSION, which the payload writes only
# when it is given one (it was never passed before 2026-10-06).
ZYPEROS_CARD = $(TARGET_DIR)/usr/share/zyperos/card
# The device's payload (mk/payload.sh), with its helpers' programs, and the
# image's /etc/zyperos/system; System Update's part where the device has it
define ZYPEROS_INSTALL_TARGET_CMDS
	DEVICE=$(ZYPEROS_DEVICE) OUT=$(ZYPEROS_CARD) VERSION="$(ZYPEROS_VERSION_NUMBER)" \
		ZYPEROS_ELF=$(@D)/$(ZYPEROS_BUILD_DIR)/zyperos.elf \
		MP4D_ELF=$(@D)/$(ZYPEROS_BUILD_DIR)/mp4d \
		$(foreach h,$(ZYDEV_HELPERS),$(ZYPEROS_HELPER_ENV_$(h))) \
		DIATOM_ELF=$(DIATOM_DIR)/build/$(ZYDEV_DIATOM_PORT)/diatom \
		VENDOR_DIR=/zyperos-vendor \
		sh $(@D)/mk/payload.sh
	$(ZYPEROS_INSTALL_SYSTEM)
	$(if $(filter update,$(ZYDEV_HELPERS)),$(ZYPEROS_INSTALL_UPDATE))
	$(if $(filter sppad,$(ZYDEV_HELPERS)),$(ZYPEROS_INSTALL_SPPAD))
endef

# The TrimUI Smart Pro's gamepad reader (ZyperOS's tools/sppad.c): every
# button and both sticks come from two serial halves, so it is part of the
# system, not the card's payload, and the start script (board/smartpro's
# init.d/brick) runs it before anything reads a button
define ZYPEROS_INSTALL_SPPAD
	$(INSTALL) -D -m 0755 $(@D)/$(ZYPEROS_BUILD_DIR)/sppad $(TARGET_DIR)/usr/lib/zyperos/sppad
endef

# Which system this is (/etc/zyperos/system), for System Update, the crash
# log and the launcher: its version, whether System Update is on it, its
# device, then its card as ZyperOS states it (config/device/<id>.system: the
# partition table and the partitions by number) and what the image has of
# the programs the launcher asks about (plat_system_has).
define ZYPEROS_INSTALL_SYSTEM
	mkdir -p $(TARGET_DIR)/etc/zyperos
	printf 'version %s\nupdate %s\ndevice %s\n' "$(ZYPEROS_VERSION_NUMBER)" \
		"$(if $(filter update,$(ZYDEV_HELPERS)),1,0)" "$(ZYPEROS_DEVICE)" \
		> $(TARGET_DIR)/etc/zyperos/system
	cat $(@D)/config/device/$(ZYPEROS_DEVICE).system >> $(TARGET_DIR)/etc/zyperos/system
	printf 'has%s\n' "$(if $(BR2_PACKAGE_USBDRIVE), usbdrive)$(if $(BR2_PACKAGE_HOSTAPD), hostapd)" \
		>> $(TARGET_DIR)/etc/zyperos/system
endef

# System Update (ZyperOS's update/ and docs/updates.md). The image holds what
# the card must not be able to change: the public keys a release list must be
# signed with (ZyperOS's res/update/keys) and the switch that runs at every
# start before anything from the card (update/apply.sh, called by
# zyperos.init); which system this is is ZYPEROS_INSTALL_SYSTEM's, above. An image built
# from a ZyperOS with no keys has System Update switched off: the program
# says to use the installer.
define ZYPEROS_INSTALL_UPDATE
	rm -rf $(TARGET_DIR)/etc/zyperos/update-keys
	mkdir -p $(TARGET_DIR)/etc/zyperos/update-keys
	for k in $(@D)/res/update/keys/*.pem; do \
		[ -f "$$k" ] || continue; \
		$(INSTALL) -m 0644 "$$k" $(TARGET_DIR)/etc/zyperos/update-keys/ || exit 1; \
	done
	$(INSTALL) -D -m 0755 $(@D)/update/apply.sh $(TARGET_DIR)/usr/lib/zyperos/update-apply.sh
endef

# Every image carries ZyperOS's px2-shot, px2-input, px2-perf, px2-snap and
# px2-open in /usr/bin, so a screenshot, a button press, a performance
# report, a game's saves set aside and a game opened are one SSH command
# away: in QEMU (tools/qemu-run.sh), on a development image, and on a release
# image with Developer Mode on. They are small, need no library (px2-snap is
# a shell script), and only root can use them (the framebuffer's GETFB2,
# uinput, the card, and the launcher's command socket, src/ctl.h, which
# px2-open shares the code of). tools/px2 on a computer drives them
# (docs/building.md).
ZYPEROS_SSH_TOOLS = px2-shot px2-input px2-perf
define ZYPEROS_BUILD_SSH_TOOLS
	for t in $(ZYPEROS_SSH_TOOLS); do \
		$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -Wall -Wextra \
			-o $(@D)/$(ZYPEROS_BUILD_DIR)/$$t $(@D)/tools/$$t.c -lm || exit 1; \
	done
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -Wall -Wextra \
		-o $(@D)/$(ZYPEROS_BUILD_DIR)/px2-open $(@D)/tools/px2-open.c $(@D)/src/ctl.c
endef
# The Pixel 2's only: they read its DRM display (GETFB2, Linux 5.7 and
# later), which the Brick's 4.9 kernel and framebuffer have not
ifeq ($(ZYPEROS_DEVICE),pixel2)
ZYPEROS_POST_BUILD_HOOKS += ZYPEROS_BUILD_SSH_TOOLS
endif
define ZYPEROS_INSTALL_SSH_TOOLS
	for t in $(ZYPEROS_SSH_TOOLS); do \
		$(INSTALL) -D -m 0755 $(@D)/$(ZYPEROS_BUILD_DIR)/$$t $(TARGET_DIR)/usr/bin/$$t || exit 1; \
	done
	$(INSTALL) -D -m 0755 $(@D)/tools/px2-snap.sh $(TARGET_DIR)/usr/bin/px2-snap
	$(INSTALL) -D -m 0755 $(@D)/$(ZYPEROS_BUILD_DIR)/px2-open $(TARGET_DIR)/usr/bin/px2-open
endef
ifeq ($(ZYPEROS_DEVICE),pixel2)
ZYPEROS_POST_INSTALL_TARGET_HOOKS += ZYPEROS_INSTALL_SSH_TOOLS
endif

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
