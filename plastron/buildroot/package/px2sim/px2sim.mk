################################################################################
#
# px2sim: the GKD Pixel 2's buttons and parts in QEMU (configs/qemu_defconfig)
#
################################################################################

PX2SIM_VERSION = 1.0
PX2SIM_SITE = $(BR2_EXTERNAL_PX2_PATH)/package/px2sim/src
PX2SIM_SITE_METHOD = local
# The daemon GPL-3.0+, the kernel module GPL-2.0, as the kernel is
PX2SIM_LICENSE = GPL-3.0+ (px2sim), GPL-2.0 (px2sim_hw)
PX2SIM_LICENSE_FILES = LICENSE

# The kernel module, kmod/px2sim_hw.c, built against this image's kernel
PX2SIM_MODULE_SUBDIRS = kmod

define PX2SIM_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -Wall -Wextra \
		-o $(@D)/px2sim $(@D)/px2sim.c
endef

define PX2SIM_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/px2sim $(TARGET_DIR)/usr/sbin/px2sim
endef

$(eval $(kernel-module))
$(eval $(generic-package))
