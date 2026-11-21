################################################################################
#
# mesa3d-px2: Mesa for the Pixel 2 (see Config.in for why not mesa3d)
#
################################################################################

MESA3D_PX2_VERSION = 26.2.6
MESA3D_PX2_SOURCE = mesa-$(MESA3D_PX2_VERSION).tar.xz
MESA3D_PX2_SITE = https://archive.mesa3d.org
MESA3D_PX2_LICENSE = MIT, SGI, Khronos
MESA3D_PX2_LICENSE_FILES = docs/license.rst
MESA3D_PX2_INSTALL_STAGING = YES
MESA3D_PX2_PROVIDES = libegl libgles libgbm

# host-mesa3d-px2: Panfrost's own GPU programs (libpan: blits, clears,
# indirect draws), written in OpenCL C, are compiled while Mesa builds, by
# three of Mesa's programs that run on the build machine, from this same
# source: mesa_clc (OpenCL C to SPIR-V, through clang), vtn_bindgen2 and
# panfrost_compile (SPIR-V to Mali code). Mesa's own way for a cross build
# (docs/drivers/panfrost.rst): build them for the host with LLVM, then build
# the device's Mesa with -Dmesa-clc=system -Dprecomp-compiler=system, which
# takes them from the PATH and needs no LLVM of its own.
MESA3D_PX2_DEPENDENCIES = \
	host-bison \
	host-flex \
	host-mesa3d-px2 \
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
	-Dgallium-rusticl=false \
	-Dmicrosoft-clc=disabled \
	-Dmesa-clc=system \
	-Dprecomp-compiler=system \
	-Dspirv-tools=disabled \
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

# The build machine's three programs alone: no driver, no API, Panfrost's
# tools for panfrost_compile. clang and SPIRV-LLVM-Translator for mesa_clc,
# SPIRV-Tools, which mesa_clc needs; libdrm's headers for Panfrost's own
# library, which panfrost_compile links. Without RTTI, as Buildroot builds
# the host's LLVM (Mesa stops otherwise).
HOST_MESA3D_PX2_DEPENDENCIES = \
	host-clang \
	host-libdrm \
	host-llvm \
	host-python-mako \
	host-python-pyyaml \
	host-spirv-llvm-translator \
	host-spirv-tools \
	host-zlib

HOST_MESA3D_PX2_CONF_ENV = \
	LLVM_CONFIG="$(HOST_DIR)/bin/llvm-config"

# host-clang, the largest of them, at no more than 8 compiles at once. At
# Buildroot's default, a CPU's count plus one (17 in a Docker VM of 16 CPUs
# and 16 GB), clang's largest files ran the VM out of memory and the
# compiler was killed (2026-11-21); host-llvm passed at 17.
MESA3D_PX2_HOST_CLANG_JOBS = $(if $(filter-out 1 2 3 4 5 6 7 8,$(PARALLEL_JOBS)),8,$(PARALLEL_JOBS))
define HOST_CLANG_BUILD_CMDS
	$(HOST_MAKE_ENV) $(HOST_CLANG_BUILD_ENV) $(BR2_CMAKE) --build $(HOST_CLANG_BUILDDIR) -j$(MESA3D_PX2_HOST_CLANG_JOBS) $(HOST_CLANG_BUILD_OPTS)
endef

HOST_MESA3D_PX2_CONF_OPTS = \
	-Dplatforms= \
	-Dgallium-drivers= \
	-Dvulkan-drivers= \
	-Dtools=panfrost \
	-Dmesa-clc=enabled \
	-Dinstall-mesa-clc=true \
	-Dprecomp-compiler=enabled \
	-Dinstall-precomp-compiler=true \
	-Dllvm=enabled \
	-Dcpp_rtti=false \
	-Dspirv-tools=enabled \
	-Degl=disabled \
	-Dgbm=disabled \
	-Dgles1=disabled \
	-Dgles2=disabled \
	-Dopengl=false \
	-Dglx=disabled \
	-Dglvnd=disabled \
	-Dshader-cache=disabled \
	-Dxmlconfig=disabled \
	-Dexpat=disabled \
	-Dzstd=disabled \
	-Dvalgrind=disabled \
	-Dlibunwind=disabled \
	-Dlmsensors=disabled \
	-Dvideo-codecs= \
	-Dgallium-va=disabled \
	-Dgallium-rusticl=false \
	-Dmicrosoft-clc=disabled \
	-Dbuild-tests=false \
	-Dhtml-docs=disabled

# Only the three programs into the host's bin/, where the device's build finds
# them (Buildroot's own host-mesa3d does the same)
define HOST_MESA3D_PX2_INSTALL_CMDS
	$(INSTALL) -D -m 0755 $(@D)/buildroot-build/src/compiler/clc/mesa_clc $(HOST_DIR)/bin/mesa_clc
	$(INSTALL) -D -m 0755 $(@D)/buildroot-build/src/compiler/spirv/vtn_bindgen2 $(HOST_DIR)/bin/vtn_bindgen2
	$(INSTALL) -D -m 0755 $(@D)/buildroot-build/src/panfrost/clc/panfrost_compile $(HOST_DIR)/bin/panfrost_compile
endef

$(eval $(meson-package))
$(eval $(host-meson-package))
