# Everything builds in Docker. `make` produces out/sdcard.img, ZyperOS on the
# base Pixel 2 system, ready to write to a card at sector 0. `make base`
# produces out/sdcard-base.img, the base system alone.
IMAGE := plastron-builder
VOLUME := plastron-work
# The x86 packing step runs on the same Debian the builder starts from
BASE_IMAGE := debian@sha256:3783cc01769c7b2b1b83a5c5ad96c815348e28ed7da68e2e3687004faa906251
# plastron lives in ZyperOS's repository, in plastron/: ZyperOS is the folder
# above, mounted read-only (the zyperos package leaves plastron/ out of its
# copy); Diatom is its diatom/ folder.
ZYPEROS_SRC ?= $(abspath $(CURDIR)/..)
# The checkout git calls the main one: in a worktree, the files git ignores
# (the cores, the ScreenScraper pair) are only there
ZYPEROS_MAIN := $(shell git -C $(ZYPEROS_SRC) worktree list --porcelain 2>/dev/null | sed -n '1s/^worktree //p')
# The libretro cores ZyperOS ships, which its mk/fetch-vendor.sh downloads,
# hash-pinned, into the main checkout's vendor/ (ignored by git, so a worktree
# has none of its own)
ZYPEROS_VENDOR ?= $(or $(ZYPEROS_MAIN),$(ZYPEROS_SRC))/vendor
# ZyperOS's ScreenScraper developer pair, compiled into the launcher as on the
# Brick (read by the zyperos package). Ignored by git and kept out of the
# source copy the build makes, so it is mounted on its own; without it the
# pair comes out empty and the build carries on.
ZYPEROS_SS_ENV ?= $(or $(ZYPEROS_MAIN),$(ZYPEROS_SRC))/.screenscraper.env
SOURCES := -v $(ZYPEROS_SRC):/zyperos:ro -v $(ZYPEROS_VENDOR):/zyperos-vendor:ro \
	$(if $(wildcard $(ZYPEROS_SS_ENV)),-v $(ZYPEROS_SS_ENV):/zyperos-ss.env:ro)
# The developer's SSH key for a development image (board/*/post-build.sh):
# local/authorized_keys, which git ignores, so a worktree has none of its own.
# The main checkout's then, so a development build from a worktree is not one
# nobody can sign in to, on the cable or over Wi-Fi. A release takes the key
# out whichever it is, and QEMU's image never keeps one.
DEV_KEYS := $(if $(wildcard $(CURDIR)/local/authorized_keys),,$(wildcard $(ZYPEROS_MAIN)/plastron/local/authorized_keys))
SOURCES += $(if $(DEV_KEYS),-v $(DEV_KEYS):/plastron-keys/authorized_keys:ro)
# Which plastron builds, and who: build.sh writes the first beside its output,
# for the SDK make sdk packs from it, and a build waiting on the same output
# shows the second (scripts/lock.sh)
PLASTRON_COMMIT := $(shell git describe --always --dirty --abbrev=12 2>/dev/null || echo unknown)
STAMP := -e PLASTRON_COMMIT=$(PLASTRON_COMMIT) -e "PLASTRON_WHO=$(USER) with plastron $(PLASTRON_COMMIT)"
# DEV=1, the development setting: the output's builds go through ccache and
# build again every input that changed, which Buildroot alone does not watch
# (scripts/build.sh says what it built and what it took as it was). Off
# unless asked for; an output keeps it once set, and leaves it with DEV=0
# CLEAN=1. Never for a release. CLEAN=1 empties the kept output first: after
# a change to the toolchain or the architecture (a development build stops
# and says so), or when what an image carries must not depend on what was
# built before it.
STAMP += -e PLASTRON_CLEAN=$(CLEAN) -e PLASTRON_DEV=$(filter 0 1,$(DEV))
DOCKER_RUN := docker run --rm -t $(STAMP) -v $(CURDIR):/src:ro $(SOURCES) -v $(VOLUME):/work $(IMAGE)

.PHONY: gauge-check charge-limit-check start-soc-check push all builder volume cores image base qemu sdk brick brick-fetch brick-inputs brick-kernel smartpro smartpro-kernel brick-bootloader smartpro-bootloader brick-probe r36s r36s-kernel bootloader shell linux-rebuild copy-out clean-output release release-pixel2 release-brick release-qemu third-party

all: image

builder:
	docker build -q -t $(IMAGE) docker

# The small cores ZyperOS builds rather than fetches (its
# mk/build-small-cores.sh): one that its systems.cfg names and ZYPEROS_VENDOR
# lacks is built there before a card is made, since mk/payload.sh refuses a
# card without it; with none missing this only looks, Docker never started
cores:
	ZYPEROS_VENDOR=$(ZYPEROS_VENDOR) sh $(ZYPEROS_SRC)/mk/build-small-cores.sh --missing

# The volume is created owned by root; the build runs as uid 1000
volume:
	docker volume create $(VOLUME) >/dev/null
	docker run --rm -v $(VOLUME):/work --user root $(IMAGE) chown 1000:1000 /work

image: builder volume cores
	$(DOCKER_RUN) sh /src/scripts/build.sh
	$(MAKE) copy-out

base: builder volume
	docker run --rm -t $(STAMP) -e FLAVOR=base -v $(CURDIR):/src:ro $(SOURCES) -v $(VOLUME):/work $(IMAGE) sh /src/scripts/build.sh
	$(MAKE) copy-out OUTPUT=output-base CARD=sdcard-base.img

# ZyperOS on QEMU's arm64 virt machine, standing in for the Pixel 2
# (configs/qemu_defconfig, docs/qemu.md), in output-qemu: out/qemu/sdcard.img,
# the kernel, out/qemu/Image, and U-Boot, out/qemu/u-boot.bin
# (scripts/build-qemu-uboot.sh), which QEMU starts and which boots the card's
# system slots as the Pixel 2's does. ZyperOS's tools/qemu-run.sh boots them,
# one VM per caller on its own copy of the card.
qemu: builder volume cores
	docker run --rm -t $(STAMP) -e FLAVOR=qemu -v $(CURDIR):/src:ro $(SOURCES) -v $(VOLUME):/work $(IMAGE) sh /src/scripts/build.sh
	$(DOCKER_RUN) sh /src/scripts/build-qemu-uboot.sh
	mkdir -p out/qemu
	docker run --rm -v $(VOLUME):/work -v $(CURDIR):/src:ro -v $(CURDIR)/out/qemu:/out --user root $(IMAGE) \
		sh -c '. /src/scripts/lock.sh && plastron_lock /work/output-qemu -s && plastron_lock /work/qemu-uboot -s && \
		cp --sparse=never /work/output-qemu/images/sdcard.img /work/output-qemu/images/Image /work/qemu-uboot/u-boot.bin /out/ && \
		chown $(shell id -u):$(shell id -g) /out/sdcard.img /out/Image /out/u-boot.bin'
	@ls -l out/qemu

# The TrimUI Brick's system (configs/brick_defconfig), in output-brick.
# Imagination's GPU libraries for it are closed and never committed: they
# come from BRICK_GPU, the userspace half of DDK 1.11@5516664 from the Sonic
# Pad SDK (lib64/ and include/), mounted at /brick-gpu.
BRICK_GPU ?= $(HOME)/brick-runtime/gpu-ddk-1.11
# And XRadio's firmware for its XR829 Wi-Fi and Bluetooth chip, closed the same
# way: from BRICK_XR829, mounted at /brick-xr829 (package/xr829-firmware)
BRICK_XR829 ?= $(HOME)/brick-runtime/xr829
# The builder's own copy of them, fetched from their publisher and checked
# (scripts/fetch-brick-inputs.sh): once, before the first make brick
brick-fetch:
	BRICK_GPU=$(BRICK_GPU) BRICK_XR829=$(BRICK_XR829) sh scripts/fetch-brick-inputs.sh

# The closed parts the Brick's build needs, there before it starts
brick-inputs:
	@[ -d "$(BRICK_GPU)/lib64" ] || { echo "no GPU libraries in $(BRICK_GPU): the Brick needs" \
		"Imagination's PowerVR DDK 1.11@5516664 userspace libraries for the GE8300," \
		"which plastron doesn't include (buildroot/package/pvr-ge8300/Config.in" \
		"says which files and where they go; make brick-fetch fetches them)" >&2; exit 1; }
	@[ -f "$(BRICK_XR829)/fw_xr829.bin" ] || { echo "no XR829 firmware in $(BRICK_XR829): the" \
		"Brick's Wi-Fi needs XRadio's firmware files, which plastron doesn't include" \
		"(buildroot/package/xr829-firmware/Config.in says which and where from; make brick-fetch" \
		"fetches them)" >&2; exit 1; }
BRICK_RUN := docker run --rm -t $(STAMP) -v $(CURDIR):/src:ro $(SOURCES) \
	-v $(BRICK_GPU):/brick-gpu:ro -v $(BRICK_XR829):/brick-xr829:ro -v $(VOLUME):/work $(IMAGE)
# Its kernel and modules come from make brick-kernel, in this volume.
brick: builder volume brick-inputs cores
	@docker run --rm -v $(VOLUME):/work $(IMAGE) test -f /work/brick-kernel/images/pvrsrvkm.ko || \
		{ echo "no Brick kernel modules yet: make brick-kernel first" >&2; exit 1; }
	$(BRICK_RUN) env FLAVOR=brick sh /src/scripts/build.sh $(BRICK_TARGET)
	$(if $(BRICK_TARGET),,$(MAKE) copy-out OUTPUT=output-brick CARD=brick-sdcard.img)

# The TrimUI Smart Pro's system (configs/smartpro_defconfig: the Brick's with
# its own device), in output-smartpro, with the Brick's closed files. Its
# kernel and modules come from make smartpro-kernel, in this volume.
smartpro: builder volume brick-inputs cores
	@docker run --rm -v $(VOLUME):/work $(IMAGE) test -f /work/smartpro-kernel/images/pvrsrvkm.ko || \
		{ echo "no Smart Pro kernel modules yet: make smartpro-kernel first" >&2; exit 1; }
	$(BRICK_RUN) env FLAVOR=smartpro sh /src/scripts/build.sh $(SMARTPRO_TARGET)
	$(if $(SMARTPRO_TARGET),,$(MAKE) copy-out OUTPUT=output-smartpro CARD=smartpro-sdcard.img)

# ZyperOS on the R36S, a test card (ZyperOS's docs/devices/r36s.md): the
# Pixel 2's system with the R36S's kernel, device trees, card and launcher
# (FLAVOR=r36s, configs/r36s_defconfig), in output-r36s, out/r36s-sdcard.img.
# Its bootloader is the Pixel 2's (board/px2/bootloader), as the Pixel 2's
# image has it. On four CPUs (R36S_CPUS, docker's --cpuset-cpus), so a build
# leaves the computer room for the rest.
R36S_CPUS ?= 0-3
r36s: builder volume cores
	docker run --rm -t --cpuset-cpus=$(R36S_CPUS) $(STAMP) -v $(CURDIR):/src:ro $(SOURCES) \
		-v $(VOLUME):/work $(IMAGE) env FLAVOR=r36s sh /src/scripts/build.sh $(R36S_TARGET)
	$(if $(R36S_TARGET),,$(MAKE) copy-out OUTPUT=output-r36s CARD=r36s-sdcard.img)

# The R36S's kernel and device tree (docs/devices/r36s.md), a start: the
# Pixel 2's kernel and config with board/r36s's fragment, ROCKNIX's R36S
# device tree, built by scripts/build-r36s-kernel.sh into the volume's
# r36s-kernel/images and copied to out/r36s-kernel. No image boots it yet.
# Four jobs at most: JOBS=N for more.
r36s-kernel: builder volume
	docker run --rm -t --cpus=$(or $(JOBS),4) -e JOBS=$(or $(JOBS),4) $(STAMP) -v $(CURDIR):/src:ro \
		-v $(VOLUME):/work $(IMAGE) sh /src/scripts/build-r36s-kernel.sh
	rm -rf out/r36s-kernel && mkdir -p out/r36s-kernel
	docker run --rm -v $(VOLUME):/work:ro $(IMAGE) tar -C /work/r36s-kernel/images -cf - . | tar -C out/r36s-kernel -xf -

# The TrimUI Brick's kernel and the modules it loads: Allwinner's Linux 4.9
# with board/brick/linux/patches, built by scripts/build-brick-kernel.sh in
# docker/brick-kernel (the GCC 10 that kernel needs) into the volume's
# brick-kernel/images, where make brick takes the modules from
BRICK_KERNEL_IMAGE := plastron-brick-kernel
brick-kernel: volume
	docker build -q -t $(BRICK_KERNEL_IMAGE) docker/brick-kernel
	docker run --rm -v $(CURDIR):/src:ro -v $(VOLUME):/work $(BRICK_KERNEL_IMAGE) \
		sh /src/scripts/build-brick-kernel.sh

# The TrimUI Smart Pro's, the same way: the Brick's patches, then
# board/smartpro/linux/patches (smartpro_defconfig, its panels) and its
# device tree, into the volume's smartpro-kernel/images
smartpro-kernel: volume
	docker build -q -t $(BRICK_KERNEL_IMAGE) docker/brick-kernel
	docker run --rm -v $(CURDIR):/src:ro -v $(VOLUME):/work $(BRICK_KERNEL_IMAGE) \
		sh /src/scripts/build-brick-kernel.sh smartpro

# The TrimUI Brick's boot loader: TF-A's BL31 and U-Boot, built by
# scripts/build-brick-bootloader.sh in docker/brick-boot (the GCC 14 it was
# tested with) into buildroot/board/brick/bootloader/u-boot-sunxi-with-spl.bin,
# which goes 128 KiB into the card. Rarely needed: the result is committed.
BRICK_BOOT_IMAGE := plastron-brick-boot
brick-bootloader: volume
	docker build -q -t $(BRICK_BOOT_IMAGE) docker/brick-boot
	docker run --rm -v $(CURDIR):/src:ro -v $(VOLUME):/work $(BRICK_BOOT_IMAGE) \
		sh /src/scripts/build-brick-bootloader.sh
	mkdir -p buildroot/board/brick/bootloader
	docker run --rm -v $(VOLUME):/work -v $(CURDIR)/buildroot/board/brick/bootloader:/out \
		--user root $(BRICK_BOOT_IMAGE) sh -c 'cp /work/brick-boot/u-boot-sunxi-with-spl.bin /out/ && \
		chown $(shell id -u):$(shell id -g) /out/u-boot-sunxi-with-spl.bin'
	@ls -l buildroot/board/brick/bootloader/u-boot-sunxi-with-spl.bin

# The TrimUI Smart Pro's, the same way: the Brick's U-Boot patches, then
# board/smartpro/u-boot/patches (its LPDDR4), into
# buildroot/board/smartpro/bootloader/u-boot-sunxi-with-spl.bin. Committed too.
smartpro-bootloader: volume
	docker build -q -t $(BRICK_BOOT_IMAGE) docker/brick-boot
	docker run --rm -v $(CURDIR):/src:ro -v $(VOLUME):/work $(BRICK_BOOT_IMAGE) \
		sh /src/scripts/build-brick-bootloader.sh smartpro
	mkdir -p buildroot/board/smartpro/bootloader
	docker run --rm -v $(VOLUME):/work -v $(CURDIR)/buildroot/board/smartpro/bootloader:/out \
		--user root $(BRICK_BOOT_IMAGE) sh -c 'cp /work/smartpro-boot/u-boot-sunxi-with-spl.bin /out/ && \
		chown $(shell id -u):$(shell id -g) /out/u-boot-sunxi-with-spl.bin'
	@ls -l buildroot/board/smartpro/bootloader/u-boot-sunxi-with-spl.bin

# The Brick panel probe (board/brick/probe): a card that shows and saves which
# panel a Brick has, to hand to other Brick owners. Kernel and boot loader only,
# none of Imagination's libraries, so it can be shared. In out/. After make
# brick-kernel, and make or make brick for its compiler and image tools.
brick-probe: builder volume
	docker run --rm -v $(CURDIR):/src:ro -v $(VOLUME):/work $(IMAGE) sh /src/scripts/build-brick-probe.sh
	mkdir -p out
	docker run --rm -v $(VOLUME):/work -v $(CURDIR)/out:/out --user root $(IMAGE) \
		sh -c 'cp /work/brick-probe/brick-panel-probe.img /out/ && chown $(shell id -u):$(shell id -g) /out/brick-panel-probe.img'
	@ls -l out/brick-panel-probe.img

# The Pixel 2's bootloader: U-Boot built from source, with Rockchip's BL31,
# DDR init and miniloader from rkbin (pinned, checked by sha256), put
# together into buildroot/board/px2/bootloader/u-boot-px2.bin. Rarely needed:
# the result is committed, and `make` only uses it.
bootloader: builder volume
	$(DOCKER_RUN) sh /src/scripts/build-bootloader.sh
	docker run --rm --platform linux/amd64 -v $(CURDIR):/src -v $(VOLUME):/work \
		$(BASE_IMAGE) sh /src/scripts/pack-bootloader.sh

# Rebuild just the kernel after changing its config, patches or device tree
linux-rebuild: builder volume
	$(DOCKER_RUN) sh /src/scripts/build.sh linux-dirclean all
	$(MAKE) copy-out

# What a release carries, in out/release (scripts/release.sh): each device's
# image without the developer's SSH key, compressed, and the licenses and
# sources of everything in it, each built clean, then SHA256SUMS over all
# four, the one file ZyperOS Installer checks downloads against. The Brick's
# kernel and modules are built again from their patches first. The version is
# ZyperOS's, from its Makefile.
#
# The two devices build side by side: Buildroot builds one package at a time,
# much of it on one core, so one build leaves most of the machine idle. Each
# one's output goes to out/release-<device>.log, its end shown if it fails.
VERSION ?= $(shell sed -n 's/^VERSION ?= //p' $(ZYPEROS_SRC)/Makefile)
release: builder volume brick-inputs brick-kernel
	@[ -n "$(VERSION)" ] || { echo "no VERSION in $(ZYPEROS_SRC)/Makefile" >&2; exit 1; }
	docker run --rm -v $(VOLUME):/work --user root $(IMAGE) rm -rf /work/release
	mkdir -p out
	$(MAKE) -j2 release-pixel2 release-brick VERSION=$(VERSION)
	docker run --rm -v $(VOLUME):/work $(IMAGE) sh -c 'cd /work/release && sha256sum *.img.xz *.tar > SHA256SUMS && cat SHA256SUMS'
	rm -rf out/release
	mkdir -p out/release
	docker run --rm -v $(VOLUME):/work -v $(CURDIR)/out:/out --user root $(IMAGE) \
		sh -c 'cp /work/release/* /out/release/ && chown -R $(shell id -u):$(shell id -g) /out/release'
	@ls -l out/release

# Each image's package list (buildroot/board/<board>/third-party-packages.md),
# made again from the last release build's manifest. Committed, and put on
# the card in the Licenses folder; make release stops when its build no
# longer matches the list
third-party:
	docker run --rm -v $(VOLUME):/work -v $(CURDIR):/src:ro $(IMAGE) python3 /src/scripts/third-party.py \
		/work/release-output-pixel2/legal-info/manifest.csv pixel2 > buildroot/board/px2/third-party-packages.md
	docker run --rm -v $(VOLUME):/work -v $(CURDIR):/src:ro $(IMAGE) python3 /src/scripts/third-party.py \
		/work/release-output-brick/legal-info/manifest.csv brick > buildroot/board/brick/third-party-packages.md

# The Smart Pro's, from its release build (make release-smartpro), on its own
third-party-smartpro:
	docker run --rm -v $(VOLUME):/work -v $(CURDIR):/src:ro $(IMAGE) python3 /src/scripts/third-party.py \
		/work/release-output-smartpro/legal-info/manifest.csv smartpro > buildroot/board/smartpro/third-party-packages.md

release-pixel2: cores
	@mkdir -p out
	$(DOCKER_RUN) sh /src/scripts/release.sh $(VERSION) pixel2 > out/release-pixel2.log 2>&1 || \
		{ tail -30 out/release-pixel2.log; exit 1; }

# The Brick's closed files there first, as release has them (its kernel from
# make brick-kernel: board/brick/post-image.sh stops without it)
release-brick: brick-inputs cores
	@mkdir -p out
	$(BRICK_RUN) sh /src/scripts/release.sh $(VERSION) brick > out/release-brick.log 2>&1 || \
		{ tail -30 out/release-brick.log; exit 1; }

# The TrimUI Smart Pro's, for its test kits (ZyperOS's tools/release.sh --kit
# smartpro): make smartpro-kernel first, as release-brick needs brick-kernel
release-smartpro: brick-inputs cores
	@mkdir -p out
	$(BRICK_RUN) sh /src/scripts/release.sh $(VERSION) smartpro > out/release-smartpro.log 2>&1 || \
		{ tail -30 out/release-smartpro.log; exit 1; }

# QEMU's image for a release, ZyperOS-v<VERSION>-qemu.tar.xz, and the
# sources of what only it has, ZyperOS-v<VERSION>-qemu-sources.tar
# (scripts/release-qemu.sh): built from empty as the devices' release images
# are, with no SSH key in it, into the volume's release/ beside theirs and
# out/release-qemu. ZyperOS's tools/release.sh builds it with the devices;
# its tools/qemu-image.sh builds it alone, to try.
release-qemu: builder volume
	@[ -n "$(VERSION)" ] || { echo "no VERSION in $(ZYPEROS_SRC)/Makefile" >&2; exit 1; }
	@mkdir -p out
	$(DOCKER_RUN) sh /src/scripts/release-qemu.sh $(VERSION) > out/release-qemu.log 2>&1 || \
		{ tail -30 out/release-qemu.log; exit 1; }
	rm -rf out/release-qemu
	mkdir -p out/release-qemu
	docker run --rm -v $(VOLUME):/work -v $(CURDIR)/out:/out --user root $(IMAGE) \
		sh -c 'cp /work/release/ZyperOS-v$(VERSION)-qemu.tar.xz /work/release/ZyperOS-v$(VERSION)-qemu-sources.tar /out/release-qemu/ && chown -R $(shell id -u):$(shell id -g) /out/release-qemu'
	@ls -l out/release-qemu

OUTPUT ?= output
CARD ?= sdcard.img
copy-out:
	mkdir -p out
	docker run --rm -v $(VOLUME):/work -v $(CURDIR):/src:ro -v $(CURDIR)/out:/out --user root $(IMAGE) \
		sh -c '. /src/scripts/lock.sh && plastron_lock /work/$(OUTPUT) -s && cp --sparse=never /work/$(OUTPUT)/images/sdcard.img /out/$(CARD) && chown $(shell id -u):$(shell id -g) /out/$(CARD)'
	@ls -l out/$(CARD)

# Put the last build's system on a GKD Pixel 2 on the USB cable, written by
# the device itself into the system slot it is not running, with that slot's
# kernel and device tree, on trial (scripts/px2-push.sh); BOOT=1 also
# boot.scr, LOADER=1 the bootloader (board/px2/bootloader/u-boot-px2.bin),
# both shared by the slots. Needs a development build's SSH key on the card
# already, and a card with two slots.
push:
	mkdir -p out/push
	docker run --rm -v $(VOLUME):/work -v $(CURDIR):/src:ro -v $(CURDIR)/out/push:/out --user root $(IMAGE) \
		sh -c '. /src/scripts/lock.sh && plastron_lock /work/output -s && \
		  cd /work/output/images && cp rootfs.squashfs Image rk3326s-gkd-pixel2.dtb boot.scr /out/ && \
		  sed -n "s/^version //p" /work/output/target/etc/zyperos/system > /out/system && \
		  chown $(shell id -u):$(shell id -g) /out/*'
	PX2_SYSTEM=$$(cat out/push/system) \
	$(if $(BOOT),PX2_BOOTSCR=out/push/boot.scr) \
	$(if $(LOADER),PX2_LOADER=buildroot/board/px2/bootloader/u-boot-px2.bin) \
		sh scripts/px2-push.sh out/push/rootfs.squashfs out/push/Image out/push/rk3326s-gkd-pixel2.dtb

# A cross SDK for building ZyperOS without this volume (scripts/sdk.sh): the
# toolchain and libraries of the last SDK_OUTPUT build, copied into a Docker
# volume of their own named by their contents, plastron-sdk-<SDK_NAME>-<id>,
# for ZyperOS's make px2-payload to mount read-only. This volume is only
# read, under its output's shared lock. Again after a build changes the
# system's libraries; one that changed nothing finds its SDK already there.
SDK_OUTPUT ?= output
SDK_NAME ?= px2
sdk: builder
	sh scripts/sdk.sh $(VOLUME) $(IMAGE) $(SDK_OUTPUT) $(SDK_NAME)

shell: builder volume
	docker run --rm -it -v $(CURDIR):/src:ro $(SOURCES) -v $(VOLUME):/work $(IMAGE) bash

clean-output:
	docker run --rm -v $(VOLUME):/work $(IMAGE) rm -rf /work/output

# The side lights' rules (package/gauge/src/lights.c), checked on this
# computer with its own compiler: no Docker, no device. Into a folder of its
# own, so two checks at once do not share a binary.
gauge-check:
	@d=$$(mktemp -d) && cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O1 -o $$d/gauge-check \
		buildroot/package/gauge/src/check.c buildroot/package/gauge/src/lights.c && \
		$$d/gauge-check; s=$$?; rm -f $$d/gauge-check; rmdir $$d; exit $$s

# The Pixel 2's charge limit (board/px2/charge-limit-check.c): the rule and
# the band taken out of its kernel patch as they stand, rk817_charge_limit
# and RK817_CHARGE_LIMIT_GAP, and run on this computer against a battery
# made of the Pixel 2's numbers, with ZyperOS's own CHARGE_LIMIT_GAP (the
# start the launcher writes) beside the driver's. No Docker, no kernel, no
# device. Into out/, which each checkout has its own of.
CHARGE_LIMIT_PATCH := buildroot/board/px2/patches/linux/9-px2-rk817-charge-limit.patch
CHARGE_LIMIT_OUT := out/charge-limit-check
charge-limit-check:
	@mkdir -p $(CHARGE_LIMIT_OUT) && \
		{ sed -n 's/^+\(#define RK817_CHARGE_LIMIT_GAP\)/\1/p' $(CHARGE_LIMIT_PATCH) && \
		  sed -n 's/^#define CHARGE_LIMIT_GAP[[:space:]]/#define ZYPEROS_CHARGE_LIMIT_GAP /p' \
			$(ZYPEROS_SRC)/src/device/pixel2.c && \
		  sed -n '/^+static void rk817_charge_limit(/,/^+}/s/^+//p' $(CHARGE_LIMIT_PATCH); \
		} > $(CHARGE_LIMIT_OUT)/rule.c && \
		cc -std=c11 -Wall -Wextra -Werror -O1 -I$(CHARGE_LIMIT_OUT) -o $(CHARGE_LIMIT_OUT)/check \
			buildroot/board/px2/charge-limit-check.c && \
		$(CHARGE_LIMIT_OUT)/check

# The Pixel 2's battery level at a start (board/px2/start-soc-check.c): the
# voltage at rest and where a start's level comes from, taken out of its
# kernel patch as they stand (rk817_rest_uv and rk817_start_from, with
# RK817_START_CHARGING_UA and rk817_start_level out of the wear3-charger-start
# patch), the full charge a start measures out of the wear patch
# (rk817_learn_fcc), the empty end out of the wear2-empty patch
# (rk817_empty_due and its RK817_EMPTY_ limits) with ZyperOS's own
# BATT_EMPTY_PCT beside them, and the OCV table, the internal resistance and
# the charge's end current out of its device tree. No Docker, no kernel, no
# device. Into out/.
START_SOC_PATCH := buildroot/board/px2/patches/linux/9-px2-rk817-start-soc.patch
START_SOC_DTS := buildroot/board/px2/dts/rk3326s-gkd-pixel2.dts
START_SOC_OUT := out/start-soc-check
# and the full charge a start measures (rk817_learn_fcc, RK817_LEARN_MAX_SOC)
WEAR_PATCH := buildroot/board/px2/patches/linux/9-px2-rk817-wear.patch
# and the empty end (rk817_empty_due), which needs the wear patch applied
# before it: Buildroot applies a folder's patches in LC_ALL=C order of their
# names, so it must sort after it
EMPTY_PATCH := buildroot/board/px2/patches/linux/9-px2-rk817-wear2-empty.patch
# and a charger's start (RK817_START_CHARGING_UA as it is now, and
# rk817_start_level), which needs all three before it
CHARGER_START_PATCH := buildroot/board/px2/patches/linux/9-px2-rk817-wear3-charger-start.patch
SOC_PATCH_ORDER := $(notdir $(START_SOC_PATCH) $(WEAR_PATCH) $(EMPTY_PATCH) $(CHARGER_START_PATCH))
start-soc-check:
	@[ "$$(printf '%s\n' $(SOC_PATCH_ORDER) | LC_ALL=C sort | tr '\n' ' ')" = "$(SOC_PATCH_ORDER) " ] || \
		{ echo "start-soc-check: the battery's patches must sort in this order, each after what it" \
		"needs: $(SOC_PATCH_ORDER)" >&2; exit 1; }
	@mkdir -p $(START_SOC_OUT) && \
		{ sed -n 's/^+\(#define RK817_START_CHARGING_UA\)/\1/p' $(CHARGER_START_PATCH) && \
		  sed -n '/^+enum rk817_start_from {/,/^+};/s/^+//p' $(START_SOC_PATCH) && \
		  sed -n '/^+static int rk817_rest_uv(/,/^+}/s/^+//p' $(START_SOC_PATCH) && \
		  sed -n '/^+static enum rk817_start_from rk817_start_from(/,/^+}/s/^+//p' $(START_SOC_PATCH) && \
		  sed -n 's/^+\(#define RK817_LEARN_MAX_SOC\)/\1/p' $(WEAR_PATCH) && \
		  sed -n '/^+static int rk817_learn_fcc(/,/^+}/s/^+//p' $(WEAR_PATCH) && \
		  sed -n 's/^+\(#define RK817_EMPTY_[A-Z_]*[[:space:]]\)/\1/p' $(EMPTY_PATCH) && \
		  sed -n '/^+static bool rk817_empty_due(/,/^+}/s/^+//p' $(EMPTY_PATCH) && \
		  sed -n '/^+static int rk817_start_level(/,/^+}/s/^+//p' $(CHARGER_START_PATCH) && \
		  sed -n 's/^#define BATT_EMPTY_PCT[[:space:]]/#define ZYPEROS_BATT_EMPTY_PCT /p' \
			$(ZYPEROS_SRC)/src/status.h; \
		} > $(START_SOC_OUT)/rule.c && \
		{ sed -n 's/.*factory-internal-resistance-micro-ohms = <\([0-9]*\)>;.*/#define PX2_R_UOHM \1/p' $(START_SOC_DTS) && \
		  sed -n 's/.*voltage-min-design-microvolt = <\([0-9]*\)>;.*/#define PX2_VMIN_UV \1/p' $(START_SOC_DTS) && \
		  sed -n 's/.*charge-term-current-microamp = <\([0-9]*\)>;.*/#define PX2_TERM_UA \1/p' $(START_SOC_DTS) && \
		  echo 'static const int PX2_OCV[][2] = {' && \
		  sed -n '/ocv-capacity-table-0/,/;/p' $(START_SOC_DTS) | grep -o '<[0-9]* [0-9]*>' | \
			sed 's/<\([0-9]*\) \([0-9]*\)>/	{ \1, \2 },/' && \
		  echo '};'; \
		} > $(START_SOC_OUT)/battery.h && \
		cc -std=c11 -Wall -Wextra -Werror -O1 -I$(START_SOC_OUT) -o $(START_SOC_OUT)/check \
			buildroot/board/px2/start-soc-check.c && \
		$(START_SOC_OUT)/check
