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

define USBDRIVE_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -Wall -Wextra \
		-o $(@D)/usbdrive $(@D)/usbdrive.c $(@D)/drive.c
endef

define USBDRIVE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/usbdrive $(TARGET_DIR)/usr/sbin/usbdrive
endef

$(eval $(generic-package))
