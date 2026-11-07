################################################################################
#
# mesa3d-px2: Mesa for the Pixel 2 (see Config.in for why not mesa3d)
#
################################################################################

MESA3D_PX2_VERSION = 25.0.7
MESA3D_PX2_SOURCE = mesa-$(MESA3D_PX2_VERSION).tar.xz
MESA3D_PX2_SITE = https://archive.mesa3d.org
MESA3D_PX2_LICENSE = MIT, SGI, Khronos
MESA3D_PX2_LICENSE_FILES = docs/license.rst
MESA3D_PX2_INSTALL_STAGING = YES
MESA3D_PX2_PROVIDES = libegl libgles libgbm

MESA3D_PX2_DEPENDENCIES = \
	host-bison \
	host-flex \
	host-python-mako \
	host-python-pyyaml \
	expat \
	libdrm \
	zlib

# Panfrost only (kmsro, which pairs it with the Rockchip display, comes with
# it), and llvmpipe beside it in QEMU's image (Config.in). GLES without
# desktop GL. No X11 or Wayland: everything draws straight to the display. The shader cache stays, so GL starts faster after the
# first time a program runs; xmlconfig (driconf files read at every start)
# doesn't.
MESA3D_PX2_CONF_OPTS = \
	-Dplatforms= \
	-Dgallium-drivers=panfrost$(if $(BR2_PACKAGE_MESA3D_PX2_LLVMPIPE),$(comma)llvmpipe) \
	-Dvulkan-drivers= \
	-Degl=enabled \
	-Dgbm=enabled \
	-Dgles1=disabled \
	-Dgles2=enabled \
	-Dopengl=true \
	-Dglx=disabled \
	-Dglvnd=disabled \
	-Dshader-cache=enabled \
	-Dxmlconfig=disabled \
	-Dexpat=enabled \
	-Dzstd=disabled \
	-Dvalgrind=disabled \
	-Dlibunwind=disabled \
	-Dlmsensors=disabled \
	-Dselinux=false \
	-Dvideo-codecs= \
	-Dgallium-va=disabled \
	-Dgallium-vdpau=disabled \
	-Dgallium-xa=disabled \
	-Dgallium-nine=false \
	-Dgallium-opencl=disabled \
	-Dgallium-rusticl=false \
	-Dmicrosoft-clc=disabled \
	-Dosmesa=false \
	-Dperfetto=false \
	-Dteflon=false \
	-Dtools= \
	-Dbuild-tests=false \
	-Dhtml-docs=disabled

# LLVM only for llvmpipe, as Buildroot's own mesa3d takes it; otherwise
# turned off, so meson never goes looking for an llvm-config
ifeq ($(BR2_PACKAGE_MESA3D_PX2_LLVMPIPE),y)
MESA3D_PX2_DEPENDENCIES += host-llvm llvm
MESA3D_PX2_MESON_EXTRA_BINARIES += llvm-config='$(STAGING_DIR)/usr/bin/llvm-config'
MESA3D_PX2_CONF_OPTS += -Dllvm=enabled -Dcpp_rtti=$(if $(BR2_PACKAGE_LLVM_RTTI),true,false)
else
MESA3D_PX2_CONF_OPTS += -Dllvm=disabled
endif

$(eval $(meson-package))
