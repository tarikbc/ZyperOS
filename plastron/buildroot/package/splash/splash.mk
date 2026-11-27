################################################################################
#
# splash: ZyperOS's boot animation, from the moment the panel lights
#
################################################################################

SPLASH_VERSION = 1.0
SPLASH_SITE = $(BR2_EXTERNAL_PX2_PATH)/package/splash/src
SPLASH_SITE_METHOD = local
SPLASH_LICENSE = GPL-3.0+
SPLASH_LICENSE_FILES = LICENSE
# The TrimUI Brick's display is a framebuffer, not DRM: splash-fb.c there,
# with the frames at its panel's 1024x768. The Smart Pro's is the Brick's
# display driver too, its framebuffer 1280x720: its own frames and pictures
# at that size, in smartpro/ (scripts/make-splash-frames.py --smartpro,
# ZyperOS's tools/genclosed.py)
ifneq ($(filter brick smartpro,$(PLASTRON_DEVICE)),)
SPLASH_DEPENDENCIES = zlib
SPLASH_SOURCE_C = splash-fb.c
SPLASH_FRAMES = $(if $(filter smartpro,$(PLASTRON_DEVICE)),smartpro/frames.bin,frames-brick.bin)
SPLASH_PICTURES = $(if $(filter smartpro,$(PLASTRON_DEVICE)),$(SPLASH_PKGDIR)/smartpro,$(SPLASH_PKGDIR))
SPLASH_LIBS = -lz
# The picture for a card without the closed files (splash-fb.c's missing),
# one a language, and the letters of the start's line, from ZyperOS's
# tools/genclosed.py
define SPLASH_INSTALL_FIRST_START
	$(INSTALL) -d $(TARGET_DIR)/usr/share/splash
	$(INSTALL) -m 0644 $(SPLASH_PICTURES)/closed-missing*.bin $(SPLASH_PKGDIR)/glyphs.bin \
		$(TARGET_DIR)/usr/share/splash/
endef
else
SPLASH_DEPENDENCIES = libdrm zlib host-pkgconf
SPLASH_SOURCE_C = splash.c
# The R36S scans 640x480 landscape. Its frames and first-start pictures are
# the Pixel's turned back (scripts/make-splash-frames.py --r36s). The Pixel's
# stay 480x640, already turned.
SPLASH_ART = $(if $(filter r36s,$(PLASTRON_DEVICE)),r36s/,)
SPLASH_FRAMES = $(SPLASH_ART)frames.bin
SPLASH_LIBS = $$($(PKG_CONFIG_HOST_BINARY) --cflags --libs libdrm) -lz -lm
# The first start's line and bar over the loop (splash.c, copy_start), in
# each of ZyperOS's languages (first-start-CODE.bin beside English's), and
# Beard's frames and line, for a card whose Appearance asks for them
# (splash.c, theme_light)
define SPLASH_INSTALL_FIRST_START
	$(INSTALL) -d $(TARGET_DIR)/usr/share/splash
	$(INSTALL) -m 0644 $(SPLASH_PKGDIR)/$(SPLASH_ART)first-start*.bin $(TARGET_DIR)/usr/share/splash/
	$(INSTALL) -D -m 0644 $(SPLASH_PKGDIR)/$(SPLASH_ART)frames-light.bin $(TARGET_DIR)/usr/share/splash/frames-light.bin
endef
endif

define SPLASH_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -Wall -Wextra \
		-o $(@D)/splash $(@D)/$(SPLASH_SOURCE_C) $(SPLASH_LIBS)
endef

# frames.bin (the Pixel's) and frames-brick.bin are ZyperOS's boot animation,
# encoded for each panel on the Mac by scripts/make-splash-frames.py; kept
# here so the build never decodes video. first-start.bin too (its
# --first-start), from ZyperOS's res/boot/first-start.
define SPLASH_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/splash $(TARGET_DIR)/usr/bin/splash
	$(INSTALL) -D -m 0644 $(SPLASH_PKGDIR)/$(SPLASH_FRAMES) $(TARGET_DIR)/usr/share/splash/frames.bin
	$(SPLASH_INSTALL_FIRST_START)
endef

# Both devices' inittabs start it through this, as their first line
define SPLASH_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(SPLASH_PKGDIR)/splash.init $(TARGET_DIR)/etc/init.d/splash
endef

$(eval $(generic-package))
