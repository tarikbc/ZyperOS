################################################################################
#
# libudev-zero: libudev.so.1 without udev, for PortMaster's Westonpack ports
#
################################################################################

LIBUDEV_ZERO_VERSION = 1.0.5
LIBUDEV_ZERO_SITE = $(call github,illiliti,libudev-zero,$(LIBUDEV_ZERO_VERSION))
LIBUDEV_ZERO_LICENSE = ISC
LIBUDEV_ZERO_LICENSE_FILES = LICENSE

# The shared library alone: its own Makefile's default target builds the
# static one too, which nothing here wants
define LIBUDEV_ZERO_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D) \
		PREFIX=/usr libudev.so.1
endef

# libudev.so.1 is both the file and its soname. Nothing in staging, and no
# libudev.so, libudev.h or libudev.pc: a package that looked for libudev
# would find it and build against it (Config.in)
define LIBUDEV_ZERO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/libudev.so.1 $(TARGET_DIR)/usr/lib/libudev.so.1
endef

$(eval $(generic-package))
