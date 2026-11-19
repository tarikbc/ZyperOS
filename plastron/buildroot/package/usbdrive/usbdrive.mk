################################################################################
#
# usbdrive: the card as a USB drive, for ZyperOS's Computer Connected
#
################################################################################

USBDRIVE_VERSION = 1.0
USBDRIVE_SITE = $(BR2_EXTERNAL_PX2_PATH)/package/usbdrive/src
USBDRIVE_SITE_METHOD = local
USBDRIVE_LICENSE = GPL-3.0+
USBDRIVE_LICENSE_FILES = LICENSE
# libdrm: the connected page is held on the panel by usbdrive itself, so its
# rows can say what moves (src/panel.h)
# The handheld's facts (src/device.h) and its panel: the Pixel 2's DRM
# (panel.c, libdrm), or the TrimUI Brick's framebuffer (panel_fb.c)
ifeq ($(ZYDEV_ID),brick)
USBDRIVE_DEPENDENCIES =
USBDRIVE_DEVICE_FLAGS = -DUSBDRIVE_BRICK
USBDRIVE_PANEL = panel_fb.c
USBDRIVE_PANEL_LIBS =
else
USBDRIVE_DEPENDENCIES = libdrm host-pkgconf
USBDRIVE_DEVICE_FLAGS =
USBDRIVE_PANEL = panel.c
USBDRIVE_PANEL_LIBS = $$($(PKG_CONFIG_HOST_BINARY) --cflags --libs libdrm)
endif

define USBDRIVE_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -Wall -Wextra $(USBDRIVE_DEVICE_FLAGS) \
		-o $(@D)/usbdrive $(@D)/usbdrive.c $(@D)/drive.c $(@D)/drivestat.c \
		$(@D)/atlas.c $(@D)/$(USBDRIVE_PANEL) \
		$(USBDRIVE_PANEL_LIBS)
endef

define USBDRIVE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/usbdrive $(TARGET_DIR)/usr/sbin/usbdrive
endef

$(eval $(generic-package))
