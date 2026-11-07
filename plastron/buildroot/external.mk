include $(sort $(wildcard $(BR2_EXTERNAL_PX2_PATH)/package/*/*.mk))

# The Pixel 2 device tree comes from ROCKNIX (board/px2/dts), including its
# own rk3326.dtsi, which replaces mainline's the same way ROCKNIX's build does.
# Copied in before every kernel build, not once after the source is patched:
# as a post-patch hook an edit here never reached a rebuilt kernel (the LED
# polarity fix, 2026-10-08), only one built from a fresh source tree.
define PX2_LINUX_ADD_DTS
	cp $(BR2_EXTERNAL_PX2_PATH)/board/px2/dts/* $(@D)/arch/arm64/boot/dts/rockchip/
	grep -q rk3326s-gkd-pixel2 $(@D)/arch/arm64/boot/dts/rockchip/Makefile || \
		echo 'dtb-$$(CONFIG_ARCH_ROCKCHIP) += rk3326s-gkd-pixel2.dtb' >> $(@D)/arch/arm64/boot/dts/rockchip/Makefile
endef
# Only for the Pixel 2's kernel: QEMU's builds none
ifneq ($(findstring pixel2,$(BR2_LINUX_KERNEL_INTREE_DTS_NAME)),)
LINUX_PRE_BUILD_HOOKS += PX2_LINUX_ADD_DTS
endif

# The Brick's SDL2 draws through Imagination's EGL on the framebuffer, with
# the video driver board/brick/patches/sdl2 adds (it is off unless asked for)
ifeq ($(BR2_PACKAGE_PVR_GE8300),y)
SDL2_CONF_OPTS += --enable-video-pvrfb
endif

# The squashfs root with the boot's files first, in the order it reads them
# (each board's squashfs-sort.txt), packed together at the image's front so
# they come off the card in a few large reads. Here, as Buildroot's squashfs
# options have no sort list (its compression options follow the compressor
# and can't be set), and this file is read after fs/*.mk. The Brick is told
# apart by its rootswitch, the GKD Pixel 2 by its usbrole
ifeq ($(BR2_PACKAGE_BRICK_ROOTSWITCH),y)
ROOTFS_SQUASHFS_ARGS += -sort $(BR2_EXTERNAL_PX2_PATH)/board/brick/squashfs-sort.txt
else ifeq ($(BR2_PACKAGE_USBROLE),y)
ROOTFS_SQUASHFS_ARGS += -sort $(BR2_EXTERNAL_PX2_PATH)/board/px2/squashfs-sort.txt
endif

# Desktop OpenGL in SDL2, on the display through EGL, for PortMaster's ports:
# Stardew Valley's MonoGame asks SDL for an OpenGL 2.1 context, which the port
# serves with its own gl4es (OpenGL over the GLES2 Panfrost already has),
# named in SDL_VIDEO_GL_DRIVER. Buildroot ties SDL2's OpenGL to X11, so it is
# asked for here. Its configure wants GL/gl.h, which Mesa installs once it
# builds desktop GL (mesa3d-px2's -Dopengl=true).
ifeq ($(BR2_PACKAGE_MESA3D_PX2),y)
SDL2_CONF_OPTS += --enable-video-opengl
endif
