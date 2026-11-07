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
# with the frames at its panel's 1024x768
ifeq ($(BR2_PACKAGE_PVR_GE8300),y)
SPLASH_DEPENDENCIES = zlib
SPLASH_SOURCE_C = splash-fb.c
SPLASH_FRAMES = frames-brick.bin
SPLASH_LIBS = -lz
else
SPLASH_DEPENDENCIES = libdrm zlib host-pkgconf
SPLASH_SOURCE_C = splash.c
SPLASH_FRAMES = frames.bin
SPLASH_LIBS = $$($(PKG_CONFIG_HOST_BINARY) --cflags --libs libdrm) -lz -lm
# The first start's line and bar over the loop (splash.c, copy_start), in
# each of ZyperOS's languages (first-start-CODE.bin beside English's), and
# Beard's frames and line, for a card whose Appearance asks for them
# (splash.c, theme_light)
define SPLASH_INSTALL_FIRST_START
	$(INSTALL) -d $(TARGET_DIR)/usr/share/splash
	$(INSTALL) -m 0644 $(SPLASH_PKGDIR)/first-start*.bin $(TARGET_DIR)/usr/share/splash/
	$(INSTALL) -D -m 0644 $(SPLASH_PKGDIR)/frames-light.bin $(TARGET_DIR)/usr/share/splash/frames-light.bin
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
