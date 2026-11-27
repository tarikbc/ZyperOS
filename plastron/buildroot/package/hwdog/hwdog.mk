################################################################################
#
# hwdog: pets the hardware watchdog, from init's first step on
#
################################################################################

HWDOG_VERSION = 1.0
HWDOG_SITE = $(BR2_EXTERNAL_PX2_PATH)/package/hwdog/src
HWDOG_SITE_METHOD = local
HWDOG_LICENSE = GPL-3.0+
HWDOG_LICENSE_FILES = LICENSE

define HWDOG_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -Wall -Wextra \
		-o $(@D)/hwdog $(@D)/hwdog.c
endef

define HWDOG_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/hwdog $(TARGET_DIR)/usr/sbin/hwdog
endef

$(eval $(generic-package))
