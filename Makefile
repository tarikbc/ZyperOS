# ZyperOS. The device builds, for the TrimUI Brick and the GKD Pixel 2, are
# plastron's (plastron/ in this repository): its Buildroot builds the
# launcher and MP4 with mk/cross.mk and lays out the card with mk/payload.sh,
# and the version below is read from here. What this Makefile does is the rest:
# the offline checks, a host build of the launcher, the cores, and the assets.
# docs/building.md says how to work on a device.

.PHONY: all clean native desk desk-brick handhelds px2-payload vendor boot system-art checkmark \
        check check-cheevos check-hare check-portal check-httpd check-idle check-taskhold check-saver check-rahash check-flacdec check-achtab check-signin check-search \
        check-raset check-xfer check-sshlink check-usblink check-devmode check-devices check-devicegen check-model check-ctl check-menus check-status check-battery check-corner check-artscrape check-artrun check-sgdb check-snapshelf check-snaps check-audioout \
        check-db check-coreopts check-mp4keys check-gpuframes check-gamedel check-gamern check-esmedia check-corepins check-targets yuvtarget check-storage check-stats check-sort check-bt check-ss check-titles check-dsv check-sidelights check-osdbar check-boost check-levels check-hud check-dmodefit check-themefile check-pkkeys check-pkover check-portkeys-flow check-portkeys check-zyhome check-theme check-config check-bios check-gamefile check-startfail check-kbnav check-tween check-menumotion check-menusnap check-cardgen check-cardart check-focusfx check-blur check-gridrule check-screencard check-focusgeo check-roundrect check-rowmacro check-emptyhome check-zyperrig check-randgame check-shelfdir \
        check-sysmark check-flip check-menushots check-controls-shots check-design check-systemart check-linebreak check-portload check-portout \
        check-zyper check-uisound check-bgm check-bgm-tracks check-sounds check-messages check-tour check-tour-shots check-textfade check-tour-colors check-about check-about-shots check-update check-sysupdate check-pminstall check-pico8 check-pico8-wget check-pico8-mp4 check-pico8-display check-pico8-keys check-pico8-quit check-p8fx check-p8fx-gl check-gpu-upright check-pmhelper check-portdoc check-boottheme check-splashbright check-bootbright check-seed-version check-px2-push check-wifi-watch check-launch-start check-crashlog check-captive check-mdns check-linksteps check-usbrole check-usbdrive check-gauge check-usbpower check-phonelink check-usblog check-clients check-keyhints check-pace check-qr check-autozone \
        check-home check-home-shots check-games check-cardpaint check-putpace landbench check-favorites-shots check-homesys-groups check-lateskip-launch check-np-footer check-vidtry check-menu-rows check-layout-shots check-artbadge check-artbadge-shots check-cardart-shots design sounds strings cjk-font check-strings check-i18n-shots check-rootswitch check-datapart check-brick-closed check-hwreport check-kitwalk \
        check-tour-previews tour-previews check-prep check-diatom check-simulator check-px2sim check-building-doc check-blog check-discord-release \
        check-release-devices check-installer release check-site-devices

all: check

# The Mac's (or Linux's) cores. mk/native.mk builds one object a source file,
# so its calls below ask for -j: unless this make already has -j, whose jobs
# they share then (make 3.81 puts -jN, or a j among the one-letter flags, in
# MAKEFLAGS).
NPROC := $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
NATIVE_J = $(if $(filter -j%,$(MAKEFLAGS))$(findstring j,$(filter-out -%,$(firstword $(MAKEFLAGS)))),,-j$(NPROC))

# What a check that links a part of src/ which says T() links beside it: the
# strings and their tables (src/i18n.h). English, as the launcher is before
# the setting is read, unless the check sets another. Up here, before the
# first rule that lists it, since make reads a rule's prerequisites as it
# meets them.
#
# The tables are the build's, not the repository's (.gitignore): written by
# tools/i18n.py from res/strings whenever one of those, or the script, is
# newer. mk/native.mk and mk/cross.mk have the same rule, and
# tools/px2-payload.sh runs it before its read-only build. Committed, their
# offsets moved with every string anyone added, and every open pull request
# that touched a string conflicted with every other after each merge.
I18N_SRC := src/i18n.c src/i18n_tables.c
src/i18n_tables.c: $(wildcard res/strings/*.txt) tools/i18n.py
	@python3 tools/i18n.py

# Everything offline, in one command. There was no umbrella target: every check
# had to be remembered by name, which is a suite in the same sense that a list
# of good intentions is a plan. A check nobody runs is a check that does not
# exist, and check-menus was about to join eight others in that state.
CHECKS = check-cheevos check-hare check-portal check-httpd check-idle check-taskhold check-saver check-rahash check-flacdec check-achtab check-rasubmit check-raoffline check-radl check-dlq check-dl-recent check-dl-look check-cheatsdl check-cheats check-signin check-noticeq check-badges check-badge-shots \
         check-raset check-xfer check-sshlink check-usblink check-devmode check-devices check-devicegen check-model check-ctl check-menus check-artscrape check-artrun check-sgdb check-snapshelf check-snaps check-audioout \
         check-db check-coreopts check-mp4keys check-gamedel check-gamern check-esmedia check-media check-snapsize check-corepins check-targets check-storage check-stats check-sort check-bt check-ss check-titles check-savedlib check-search \
         check-mp4lib check-mp4queue check-mp4art check-mp4lock check-mp4look check-mp4views check-mp4resume check-bands check-covercolor check-mp4frame check-mp4size check-p2share check-webtheme check-p2ws check-p2codec check-p2d check-play check-p2 check-resident check-spawn check-controls check-remap check-hotkeys check-rumble check-eq check-eqdsp check-eqplugin check-controls-shots check-logpack \
         check-clock check-autozone check-status check-battery check-battery-eta check-dslang check-pacing check-sgbmode check-border check-handframe check-gpuframes check-dsresume check-corestate check-core-stamp check-core-room check-px2-perf check-px2-snap check-px2 check-lowbatt check-battempty check-powerhold check-homesys check-collect check-shotname check-heat check-charging check-corner check-dsv check-sidelights check-osdbar check-boost check-levels check-hud check-dmodefit check-themefile check-pkkeys check-pkover check-portkeys-flow check-portkeys check-zyhome check-theme check-config check-bios check-gamefile check-startfail check-kbnav check-randgame check-shelfdir check-look \
         check-tween check-menumotion check-menusnap check-cardgen check-cardart check-focusfx check-blur check-gridrule check-screencard check-focusgeo check-roundrect check-rowmacro check-emptyhome check-zyperrig check-sysmark check-flip check-menushots check-design \
         check-systemart check-linebreak check-portload check-portout check-zyper check-uisound check-bgm check-bgm-tracks check-sounds check-messages \
         check-tour check-tour-shots check-tour-previews check-textfade check-glyphs check-scale check-shaderdemo check-shaderpic check-screenfx check-ds check-layout-ratchet check-flex check-hover check-hover-sweep check-tour-colors check-about check-about-shots check-update check-sysupdate check-pminstall check-pico8 check-pico8-wget check-pico8-mp4 check-pico8-display check-pico8-keys check-pico8-quit check-p8fx check-p8fx-gl check-gpu-upright check-pmhelper check-portlibs check-pmart check-portdoc check-boottheme check-splashbright check-bootbright check-seed-version check-panelgamma check-panelgamma-sunxi check-nightmode check-autotheme check-displayback check-px2-push check-wifi-watch check-launch-start check-crashlog check-captive check-mdns check-linksteps check-usbrole check-usbdrive check-gauge check-start-soc check-usbpower check-phonelink check-usblog check-clients check-keyhints check-pace check-qr \
         check-home check-home-shots check-games check-cardpaint check-putpace check-favorites-shots check-homesys-groups check-lateskip-launch check-np-footer check-vidtry check-menu-rows check-layout-shots check-artbadge check-artbadge-shots check-cardart-shots checkmark \
         check-installer check-from-rogue check-importers check-diatom check-simulator check-qemu-image check-desk check-homekeys check-videoview check-inrepeat check-keymap check-handhelds check-px2sim check-building-doc check-blog check-discord-release \
         check-release-devices check-strings check-i18n-shots check-rootswitch check-datapart check-brick-closed check-hwreport check-kitbuild check-kitwalk check-ingame-colors

# All of them at once, as many at a time as there are cores (CHECK_JOBS=1 for
# one after another), each into build-native/check-logs/: tools/check-run.py
# prints a line as each ends and the whole log of any that failed. One after
# another the suite took 167 s on an M3 (2026-11-06), with fifteen cores idle.
.PHONY: $(CHECKS)
# CHECKS_SKIP="check-a check-b" leaves those out: CI's Linux job names the
# checks that do not pass on Linux yet, which its macOS job still runs.
CHECKS_SKIP ?=
check: check-prep
	@$(if $(strip $(CHECKS_SKIP)),echo "check: left out (CHECKS_SKIP): $(strip $(CHECKS_SKIP))")
	@python3 tools/check-run.py $(filter-out $(CHECKS_SKIP),$(CHECKS))

# What more than one check builds, built once before any of them starts: the
# host launcher, which nine checks draw with, and the ScreenScraper header it
# shares with check-ss (without SDL2, the header alone); and player 2's p2d,
# its feed, the codec check and the theme writer's check, which check-p2d,
# check-p2codec, check-webtheme and check-play run.
check-prep: src/i18n_tables.c
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native; \
	else \
		$(MAKE) --no-print-directory -f mk/native.mk VERSION=$(VERSION) creds; \
	fi
	@$(MAKE) --no-print-directory build-native/p2d build-native/p2d-feed build-native/p2codec-check build-native/webtheme-check

# Diatom's own suite (diatom/Makefile's check) on its desktop port: SDL2's
# dummy drivers and the stub core, no device and no ROM. It was only ever run
# by hand. Its state-plane and resident checks need the desktop build and the
# stub, so they are built first. Skipped without SDL2.
check-diatom:
	@if pkg-config --exists sdl2 2>/dev/null || command -v sdl2-config > /dev/null; then \
		$(MAKE) --no-print-directory -C diatom $(NATIVE_J) all stub && \
		$(MAKE) --no-print-directory -C diatom check && \
		$(MAKE) --no-print-directory build-native/gamezone-check && \
		./build-native/gamezone-check diatom/build/desktop/diatom; \
	else \
		echo "check-diatom: skipped, no SDL2"; \
	fi

# zyperos.com's blog (mk/blog.py): the whole site built in a scratch folder as
# Cloudflare Pages builds it, then each post's title and description lengths,
# its structured data, every link and picture on the blog and the landing page
# reaching a file or a path in _redirects (/tip), every post in the sitemap and llms.txt, and the blog's
# colors still the landing page's.
check-blog:
	@python3 mk/blog.py --check

# zyperos.com with any number of handhelds (site/devices.json): the site built
# with made-up ones added (three, then five in all), each at a computer's and
# a phone's width in headless Chrome, with no error, no sideways scroll and
# every pick followed. Needs Google Chrome; --shots DIR keeps the heroes.
check-site-devices:
	@python3 tools/check-site-devices.py

# The release notes as tools/release.sh posts them in the Discord's #releases:
# within Discord's limits, with What's new and the release's link
check-discord-release:
	@python3 tools/discord-release.py --check

# The devices a release carries (config/devices) and the release notes'
# "Tested on <id>:" lines tools/release.sh publishes nothing without: the
# reader on sample notes and lists, then each listed device's profile and
# res/update/min-system-<id>, which tools/update-pack.py reads
check-release-devices:
	@sh tools/release-devices.sh check

# docs/building.md's table names every check in CHECKS and nothing else:
# the table had 32 of 91 rows (2026-11-06) and said nothing of the rest.
check-building-doc:
	@python3 tools/building-doc-check.py

# Desk mode (docs/building.md, "Work on the UI without a device"): the
# launcher in a window as the Pixel 2, or as the Brick, on a sample card, a
# game in it on Diatom's desktop build. tools/desk takes more (--zoom, --roms,
# --lang, --fresh) and drives a running window.
desk:
	@tools/desk run --device pixel2

desk-brick:
	@tools/desk run --device brick

# Desk mode in SDL's dummy window, both handhelds, over its control socket
check-desk: native
	@python3 tools/desk-check.py

# The home's d-pad held, in desk mode: the Grid's rows to each end, Spotlight's
# groups forward
check-homekeys: native
	@python3 tools/homekeys-check.py

# The Video view in desk mode: its snap playing holds the screensaver
check-videoview: native
	@python3 tools/videoview-check.py

# No button read twice by in_repeat in one step (a held one turned round)
check-inrepeat:
	@python3 tools/inrepeat-check.py

# One key map: src/device/keymap.h, the Simulator's and Diatom's window's
check-keymap:
	@python3 tools/keymap-check.py

# The Simulator's handhelds (simulator/Sources/SimCore/Handhelds.swift), written
# from the device files themselves (each src/device/<id>_model.c's flat
# drawing, src/device/td_model.h) by tools/handhelds-swift.c, so the Simulator
# draws every handheld as desk mode does, from the same numbers. `make
# handhelds` writes it; check-handhelds fails while it is behind them.
HANDHELDS_SRC := src/profile.c $(wildcard src/device/*_profile.c) src/device/td_model.c \
                 $(filter-out src/device/td_model.c,$(wildcard src/device/*_model.c)) \
                 $(wildcard src/device/*_coreopts.c)
build-native/handhelds-swift: tools/handhelds-swift.c $(HANDHELDS_SRC) src/device/td_model.h \
                              src/device/profile.h FORCE
	@mkdir -p build-native
	@$(CC) -std=gnu11 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -O1 -Isrc \
	      -DZYPEROS_PROFILE=device_profile_desktop -DZYPEROS_PROFILES_ALL \
	      -o $@ tools/handhelds-swift.c $(HANDHELDS_SRC)

handhelds: build-native/handhelds-swift
	@./build-native/handhelds-swift > build-native/Handhelds.swift
	@mv build-native/Handhelds.swift simulator/Sources/SimCore/Handhelds.swift
	@echo "handhelds: wrote simulator/Sources/SimCore/Handhelds.swift"

check-handhelds: build-native/handhelds-swift
	@./build-native/handhelds-swift > build-native/Handhelds.swift
	@if cmp -s build-native/Handhelds.swift simulator/Sources/SimCore/Handhelds.swift; then \
		echo "handhelds: simulator/Sources/SimCore/Handhelds.swift is the device files' own"; \
	else \
		diff -u simulator/Sources/SimCore/Handhelds.swift build-native/Handhelds.swift | head -30; \
		echo "check-handhelds: simulator/Sources/SimCore/Handhelds.swift is behind src/device/*_model.c: run make handhelds"; \
		exit 1; \
	fi

# The simulator's tests (simulator/Tests: its D-Bus display and VNC clients, the px2sim link, the
# state lines and the control socket's commands), with Swift's own build kept
# in build-native/. Skipped where there is no Swift, and off macOS: the
# simulator is an AppKit app (Ubuntu's runner has Swift, and its build fails).
check-simulator:
	@if [ "$$(uname -s)" = Darwin ] && command -v swift > /dev/null 2>&1; then \
		cd simulator && swift test --scratch-path ../build-native/simulator; \
	else \
		echo "check-simulator: skipped, not macOS or no Swift"; \
	fi

# The published QEMU image's way to a developer, offline on a stand-in
# archive: tools/qemu-run.sh fetch and its pin, and the SSH key QEMU hands
# the VM in place of one in the image (tools/qemu-image-check.sh)
check-qemu-image:
	@sh tools/qemu-image-check.sh

# px2sim's choice of the keyboard it takes (plastron/.../px2sim/src/kbdsel.h):
# QEMU's, never a program's uinput one (gptokeyb's Fake Keyboard). Host
# compiler only, no VM.
check-px2sim:
	@mkdir -p build-native && cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
		-o build-native/kbdsel-check plastron/buildroot/package/px2sim/tests/kbdsel-check.c && \
		build-native/kbdsel-check

# One version number: plastron reads it from here for the card and the About
# page (its package/zyperos/zyperos.mk), and ZyperOS Installer has it too.
# ZyperOS counts from 0.1.0. Change it with
# tools/set-version.sh, which sets the installer's with it.
VERSION ?= 1.0.6

# A release, from this Mac (tools/release.sh): the card image of each device
# in config/devices built clean by plastron from this commit; PUBLISH=1 then
# makes the draft GitHub release and pushes the tag, and GitHub adds ZyperOS
# Installer for macOS, Windows and Linux and publishes it
# (.github/workflows/release.yml). KIT=brick makes a test kit of those
# devices instead, at a pre-release version (tools/release.sh --kit).
release:
	@bash tools/release.sh $(if $(KIT),--kit "$(KIT)") $(if $(PUBLISH),--publish)

# ZyperOS Installer (installer/): its version is ZyperOS's, and its page
# wears this repository's colors, fonts and Zyper. Its Rust tests run in CI
# (.github/workflows/installer.yml) and with cargo test in installer/src-tauri.
check-installer:
	@sh tools/check-version.sh
	@sh installer/tools/check-design.sh

# The ScreenScraper developer pair, from .screenscraper.env in this directory
# (gitignored, never committed). A value already in the environment wins, and
# no file is still a supported build: the defines come out empty and art falls
# back to libretro. plastron's build reads the same file for the devices.
SS_ENV := .screenscraper.env
ss_value = $(shell [ -f $(SS_ENV) ] && sed -n 's/^$(1)=//p' $(SS_ENV) | head -1)
SS_DEVID ?= $(call ss_value,SS_DEVID)
SS_DEVPASS ?= $(call ss_value,SS_DEVPASS)
export SS_DEVID SS_DEVPASS

# The check binaries are rebuilt every time, deliberately.
#
# They take about a second each, and make's mtime comparison is second-granular
# - so editing a source and re-running a check inside the same second silently
# tests the PREVIOUS binary. That is not theoretical: it happened three times on
# 2026-08-30 while checking whether a check actually catches the bug it claims
# to, and each time a clean source read as a failing one.
#
# A check that quietly tests the wrong binary is worse than no check.
FORCE:

# DraStic's DS saves taken over by DSperate (src/dsv.c): footer off, the
# chip's bytes kept, an .srm already there never touched.
check-dsv: build-native/dsv-check
	@./build-native/dsv-check

build-native/dsv-check: tools/dsv-check.c src/dsv.c src/dsv.h src/atomic.c src/atomic.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -o $@ tools/dsv-check.c src/dsv.c src/atomic.c

# The achievement half, checked on the host. It parses a file and filters a
# list - no SDL, no device, no emulator - and every one of its claims fails
# invisibly on hardware, which is the argument for checking it here.
check-cheevos: build-native/cheevos-check
	@./build-native/cheevos-check

build-native/cheevos-check: tools/cheevos-check.c src/cheevos.c src/cheevos.h \
                            src/atomic.c src/atomic.h src/db.c src/db.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/cheevos-check.c src/cheevos.c src/atomic.c src/db.c -lsqlite3

# Hare's routes: is anything reachable without the PIN? check-xfer proves a
# path cannot climb out of a root; this proves the routes actually ask it, and
# that they are behind the lock - separate claims, and the ones that regress
# when an endpoint is added.
check-hare: build-native/hare-check
	@./build-native/hare-check

# A game's picture and video in a listing are the launcher's lookup
# (src/esmedia.h's game_media), so the lookup is linked as the launcher has it.
HARE_GAME_MEDIA = src/esmedia.c src/library.c src/snaps.c src/db.c

build-native/hare-check: tools/hare-check.c src/hare.c src/sheet.c src/clients.c src/httpd.c src/xfer.c src/mp4lib.c src/captive.c $(HARE_GAME_MEDIA) $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -O1 -g \
	      -DHARE_PORTS=0 -DPICK_TMP='"$(CURDIR)/build-native/hare-check-albumpick"' \
	      -DART_NAMES_TMP='"$(CURDIR)/build-native/hare-check-artnames"' -DREPO_WEB='"$(CURDIR)/res/web"' \
	      -o $@ tools/hare-check.c src/hare.c src/sheet.c src/clients.c src/httpd.c src/xfer.c src/mp4lib.c src/captive.c \
	      $(HARE_GAME_MEDIA) $(I18N_SRC) -lz -lsqlite3 -lpthread

# The File Transfer page on the host, over a scratch card: the same routes and
# server, for a browser and for tools/readme-shots.py's pictures of the page.
# The page itself, in headless Chrome over that server: its folders' art, the
# hidden dot folders, the room, the time left, the handheld's stop, the
# update over the cable and its name after the link it came over
# (tools/portal-check.py). Chrome is needed.
check-portal: build-native/portal-serve
	@python3 tools/portal-check.py

build-native/portal-serve: tools/portal-serve.c src/hare.c src/sheet.c src/clients.c src/httpd.c src/xfer.c src/mp4lib.c src/captive.c src/look.c $(HARE_GAME_MEDIA) $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -O1 -g \
	      -DHARE_PORTS=0 -DPICK_TMP='"$(CURDIR)/build-native/portal-serve-albumpick"' \
	      -DART_NAMES_TMP='"$(CURDIR)/build-native/portal-serve-artnames"' \
	      -o $@ tools/portal-serve.c src/hare.c src/sheet.c src/clients.c src/httpd.c src/xfer.c src/mp4lib.c src/captive.c src/look.c \
	      $(HARE_GAME_MEDIA) $(I18N_SRC) -lz -lm -lsqlite3 -lpthread

# Hare's transport, driven by a real client over a real socket. An HTTP parser
# is where "looks right" and "is right" part company: every browser sends the
# well-formed case, and only the interesting failures send anything else.
check-httpd: build-native/httpd-check
	@./build-native/httpd-check

build-native/httpd-check: tools/httpd-check.c src/httpd.c src/httpd.h src/xfer.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/httpd-check.c src/httpd.c -lz

# Hare's path safety: can a browser on the LAN climb out of the three roots?
# Every other check in here protects a feature; this one protects the device.
check-xfer: build-native/xfer-check
	@./build-native/xfer-check

# What a menu contains, for a given state. The point of the menu design: a
# screen's build function takes state and produces rows with no renderer and
# no device, so this is answerable here instead of by looking at a handheld.
# Every menu defect this screen has had was some form of "nobody noticed the
# list was wrong", and every one of them needed a device to see.
#
# It links the pure halves of the screens and NOT SDL. If it ever needs SDL to
# link, the menu design has failed - reopen it rather than adding the flag.
check-menus: build-native/menu-check
	@./build-native/menu-check

# Where sound goes, given a cable, a headset and a setting. Eight combinations
# and a five-line rule, which is exactly the kind of thing that is obviously
# right until a headset connects mid-game and it is not.
check-audioout: build-native/audioout-check
	@./build-native/audioout-check

build-native/audioout-check: tools/audioout-check.c src/audioout.c \
                            src/audioout.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/audioout-check.c src/audioout.c $(I18N_SRC)

# The volume and brightness bar, drawn by src/osdbar.c for the launcher, Diatom
# and portkeys alike: a segment lit per step and no more, at both scales, and
# its in, hold and out at the moments the three ask about.
check-osdbar: build-native/osdbar-check
	@./build-native/osdbar-check

# The boost past the top of the volume and brightness (src/boost.h): the
# hold and the three presses that open it, the charge draining as it filled,
# the held key stopping at the first cell, the way back down, and where it
# may not open (MP4's lock screen, the menus).
check-boost: build-native/boost-check
	@./build-native/boost-check

# The launcher's level keys (src/levels.c) as the screens call them, the
# device, the database and MP4 stubbed (tools/levels-check.c): with MP4 heard
# behind the home, a shelf or a menu, its boost works there as on its own
# screens (the cells on the bar, the hold and the presses that open it, a
# press down emptying a cell first, each cell told to MP4); over a paused
# game, the game's the same way; a running game's steps told to MP4 at once;
# with neither, the menus' own. SDL's headers for platform.h; skipped
# without them.
check-levels:
	@if command -v sdl2-config > /dev/null 2>&1; then \
		$(MAKE) --no-print-directory build-native/levels-check && ./build-native/levels-check; \
	else \
		echo "check-levels: skipped, no SDL2"; \
	fi

# The Display list's previews (src/dmodefit.h): each mode's rect as the
# launcher works it out, against Diatom's own arithmetic (diatom/src/scale.c),
# and Square Pixels' sizes for the systems #346 names, on both handhelds
check-dmodefit: build-native/dmodefit-check
	@./build-native/dmodefit-check

build-native/dmodefit-check: tools/dmodefit-check.c src/dmodefit.c src/dmodefit.h diatom/src/scale.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Idiatom/src -Idiatom/include \
	      -o $@ tools/dmodefit-check.c src/dmodefit.c diatom/src/scale.c

# MENU > Power Button's rule (src/powerhold.h): a press turns the handheld off
# in Press mode and a hold of one second in Hold mode (its own time, the
# charging screen's and MP4's two kept), once per time down;
# the pill's progress and the waits that keep it moving.
check-powerhold: build-native/powerhold-check
	@./build-native/powerhold-check

build-native/powerhold-check: tools/powerhold-check.c src/powerhold.c src/powerhold.h src/osdbar.h \
                              src/design.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -o $@ tools/powerhold-check.c src/powerhold.c

# MENU > Display > Home Systems' setting (src/homesys.h): the word of the tags
# that are off, read and written back, a hand edit's spaces and repeats, and
# whole tags only when the word is cut.
check-homesys: build-native/homesys-check
	@./build-native/homesys-check

build-native/homesys-check: tools/homesys-check.c src/homesys.c src/homesys.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -o $@ tools/homesys-check.c src/homesys.c

# A screenshot's file name (src/shotname.h, DESIGN.md 11.68): read as the
# Screenshot hotkey writes it, every other file left alone, its time kept by
# the calendar and read back to the same name, and the Screenshots shelf's
# three orders.
check-shotname: build-native/shotname-check
	@./build-native/shotname-check

build-native/shotname-check: tools/shotname-check.c src/shotname.c src/shotname.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -o $@ tools/shotname-check.c src/shotname.c

# Collections on the card (src/collect.h): the lists read in their every
# form and kept as written, added to and taken from, made, renamed and
# deleted, and a game's rename and delete followed into every list.
check-collect: build-native/collect-check
	@./build-native/collect-check

build-native/collect-check: tools/collect-check.c src/collect.c src/collect.h src/atomic.c src/atomic.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -o $@ tools/collect-check.c src/collect.c src/atomic.c

# The FPS readout (src/hud.h): its text, its pill left of the level bar at
# both scales, its colors, and Diatom's copies the same. The glyphs against
# tools/genhudglyphs.py are check-genhudglyphs, beside check-genfirststart and
# out of the suite as it is: another Pillow or FreeType draws other pixels
check-hud: build-native/hud-check
	@./build-native/hud-check

build-native/hud-check: tools/hud-check.c src/hud.c src/hud.h src/hudglyphs.h src/osdbar.c src/osdbar.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/hud-check.c src/hud.c src/osdbar.c -lm

build-native/boost-check: tools/boost-check.c src/boost.h src/osdbar.h src/design.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -o $@ tools/boost-check.c -lm

build-native/levels-check: tools/levels-check.c src/levels.c src/levels.h src/boost.h src/osdbar.h \
                           src/design.h src/platform.h src/mp4c.h FORCE
	@mkdir -p build-native
	@$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g $$(sdl2-config --cflags) \
	      -o $@ tools/levels-check.c src/levels.c -lm

build-native/osdbar-check: tools/osdbar-check.c src/osdbar.c src/osdbar.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/osdbar-check.c src/osdbar.c -lm

# The shelf's status corner (src/status.c): the battery's low and critical
# warnings, the signal's steps and wpa_cli signal_poll read for them, and the
# marks drawn as the status lab drew them, in both themes, from the tokens.
check-status: build-native/status-check
	@./build-native/status-check

build-native/status-check: tools/status-check.c src/status.c src/status.h \
                          src/design.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/status-check.c src/status.c src/look.c -lm

# Charger changes reach the shared battery cache within 250 ms at the shelf's
# 16 ms poll rate. Stable samples request no redraw; capacity stays at 5 s.
# And the charger-online rule both devices read the cable by
# (src/charging_rule.h). Plain C only: this does not prove the kernel's
# hardware propagation time.
check-battery: build-native/battery-check
	@./build-native/battery-check

build-native/battery-check: tools/battery-check.c src/battery_poll.c src/battery_poll.h \
                            src/charging_rule.c src/charging_rule.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O1 -g \
	      -o $@ tools/battery-check.c src/battery_poll.c src/charging_rule.c

# The time left on the battery (src/battery_eta.h): the estimate from how long
# a percent takes to fall, against a simulated gauge at several loads, across
# the clock's wrap, through a change of load, a stall, the cable and a gauge
# that corrects itself. Plain C, no device.
check-battery-eta: build-native/battery-eta-check
	@./build-native/battery-eta-check

build-native/battery-eta-check: tools/battery-eta-check.c src/battery_eta.c src/battery_eta.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O1 -g \
	      -o $@ tools/battery-eta-check.c src/battery_eta.c -lm

# Timing, a system menu row (src/pacing.h): Smooth or Exact, the words kept
# and sent as RUN's pace=, the system's own default for a word that is none,
# the database key by tag, and stepping round. The row on the menu is
# check-menus'; RUN's keys and the systems with the row, check-config's; what
# Diatom does with pace=, diatom's check-displaylock.
check-pacing: build-native/pacing-check
	@./build-native/pacing-check

build-native/pacing-check: tools/pacing-check.c src/pacing.c src/pacing.h src/i18n_ids.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O1 -g -Isrc -o $@ tools/pacing-check.c src/pacing.c

# Super Game Boy, a system menu row (src/sgbmode.h): Off or Its Games, the
# words kept and sent as RUN's sgb=, Off for a word that is none,
# the database key by tag, stepping round, and the shelves that have it (the
# Game Boy's and the Color's, on mGBA). What Diatom does with sgb=, diatom's
# check-sgb.
check-sgbmode: build-native/sgbmode-check
	@./build-native/sgbmode-check

build-native/sgbmode-check: tools/sgbmode-check.c src/sgbmode.c src/sgbmode.h src/i18n_ids.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O1 -g -Isrc -o $@ tools/sgbmode-check.c src/sgbmode.c

# Border, a display row (src/border.h): its kinds and words, what the row
# offers and steps round, a kept kind it cannot show reading Off, the room
# rule at each mode on the Pixel 2's and the Brick's surfaces, NextUI's tags.
# What Diatom draws, diatom's check-border and check-border-e2e.
check-border: build-native/border-check
	@./build-native/border-check

build-native/border-check: tools/border-check.c src/border.c src/border.h src/dmodefit.c src/dmodefit.h src/i18n_ids.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O1 -g -Isrc -o $@ tools/border-check.c src/border.c src/dmodefit.c -lm

# The handheld frames (src/handframe.h): Handheld where Sharp Pixels puts the
# game, Whole a step smaller with the lens in the middle and never under 2x,
# a game pixel a dot, straight edges on whole pixels, the Color's arc where
# its model has it, every part inside the body, the Color's six colors. On
# 640x480 and 1024x768. The sheet that lays each frame under its outlines is
# tools/handframe-sheet.py.
check-handframe: build-native/handframe-check
	@./build-native/handframe-check

build-native/handframe-check: tools/handframe-check.c src/handframe.c src/handframe.h src/handframe_models.c src/dmodefit.c src/dmodefit.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O1 -g -Isrc -o $@ tools/handframe-check.c src/handframe.c src/handframe_models.c src/dmodefit.c -lm

# The Nintendo DS's language (src/dslang.h): every language MENU > Language has
# maps to the DS's own or to English, the core's option and its values are the
# patch's (mk/patches/dsperate-lr-language.patch), and the launcher sends it
# first with every DS game and with no other. Plain C for the table; the
# launcher itself, built for the host, for what it sends.
check-dslang: build-native/dslang-check
	@./build-native/dslang-check
	@sh tools/dslang-check.sh

# The systems whose cores need GPU frames (config/systems.cfg's needs lines):
# no shelf for them on a handheld without GPU frames (the Brick's profile),
# every shelf on one with them (the Pixel 2's). The launcher, built for the
# host, scanning a scratch card.
check-gpuframes:
	@sh tools/gpuframes-check.sh

# Every picture Diatom's GPU ports draw, the right way up on the panel: the
# shared presenting code (diatom/port/gles_*.h) built with each port's hooks
# (diatom/test/upright_test.c) and run on Mesa's llvmpipe, each frame read
# back as that device's display reads it, the TrimUI Brick's page of fb0 from
# its first byte (issue #419), the GKD Pixel 2's window from its top row:
# frames at every filter, a DS frame, a GPU core's frames, the Border, a
# notice, the level bar, On-Screen Stats, the low-battery disc and captures;
# and the launcher's CRT Curved bent from a target texture, as the Brick
# bends it, on SDL's OpenGL ES 2 renderer (tools/gpu-upright-bend.c).
# Linux only (Mesa's EGL); elsewhere it says it skipped, and UPRIGHT_DOCKER=1
# runs it in tools/p8fx-gl.Dockerfile's Linux (tools/gpu-upright-check.py)
check-gpu-upright:
	@python3 tools/gpu-upright-check.py $(if $(UPRIGHT_DOCKER),--docker)

build-native/dslang-check: tools/dslang-check.c src/dslang.c src/dslang.h src/i18n_ids.h \
                           mk/patches/dsperate-lr-language.patch FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O1 -g \
	      -o $@ tools/dslang-check.c src/dslang.c

# A game's resume when its auto state was left by the shelf's other core
# (src/corestate.h): Diatom's state header read back, and the start fresh or
# resume. Plain C, no device.
check-corestate: build-native/corestate-check
	@./build-native/corestate-check

build-native/corestate-check: tools/corestate-check.c src/corestate.c src/corestate.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O1 -g \
	      -o $@ tools/corestate-check.c src/corestate.c

# A Nintendo DS game's resume by its language (src/dsresume.h): the firmware's
# language as DSperate's direct boot takes it, the language a game starts in
# for each option, the .lang beside its auto state, and the start fresh or
# resume. Plain C, no device.
check-dsresume: build-native/dsresume-check
	@./build-native/dsresume-check

build-native/dsresume-check: tools/dsresume-check.c src/dsresume.c src/dsresume.h \
                             src/atomic.c src/atomic.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/dsresume-check.c src/dsresume.c src/atomic.c

# The patched cores' stamps (mk/core-stamp.sh): a stamp written for a core
# passes, and a core built before a patch changed, a core changed after its
# build, a core with no stamp and a patch no core claims each fail, with what
# to run. On a scratch vendor/ of made-up cores; the real one is the
# payload's to hold, and release.sh's.
check-core-stamp:
	@sh tools/core-stamp-check.sh

# Diatom's resident registry against the cores systems.cfg ships
# (tools/core-room-check.sh: 1.0.5 had 34 for 32 slots)
check-core-room:
	@sh tools/core-room-check.sh

# The debugging tools (docs/building.md, "Debugging on the device"). px2-perf's
# percentiles, frame times, CPU ticks and speed against the clock
# (tools/px2-perf.h), plain C on this computer; px2-snap's choice of a game's
# files and rows, put back byte for byte, on a scratch card with the host
# launcher (skips without SDL2 or sqlite3); and tools/px2's arguments, card
# and lock, with a stand-in for ssh. None reaches a device.
check-px2-perf: build-native/px2-perf-check
	@./build-native/px2-perf-check

build-native/px2-perf-check: tools/px2-perf-check.c tools/px2-perf.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O1 -g -o $@ tools/px2-perf-check.c -lm

check-px2-snap:
	@sh tools/px2-snap-check.sh

check-px2:
	@sh tools/px2-check.sh

# The start with a battery too low to play (src/lowbatt.h): which start shows
# the screen, its hold, POWER, the charger's moment, the fresh reading that
# decides the power-off. Plain C, no device.
check-lowbatt: build-native/lowbatt-check
	@./build-native/lowbatt-check

build-native/lowbatt-check: tools/lowbatt-check.c src/lowbatt.c src/lowbatt.h src/status.h src/design.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O1 -g \
	      -o $@ tools/lowbatt-check.c src/lowbatt.c

# The battery run flat while the handheld runs (src/battempty.h), the rule
# battery_empty and portkeys go by: the level at BATT_EMPTY_PCT or under
# twice in a row, or the voltage under the device's empty voltage three times
# in a row whatever the level says; never on the cable, never on one stray
# reading. Plain C, no device.
check-battempty: build-native/battempty-check
	@./build-native/battempty-check

build-native/battempty-check: tools/battempty-check.c src/battempty.c src/battempty.h src/status.h \
                              src/device/pixel2_levels.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O1 -g \
	      -o $@ tools/battempty-check.c src/battempty.c

# The charging screen (src/charging.h): the charger's kind, its lines, the
# time to full through CC and CV, and its rules. Plain C, no device.
check-charging: build-native/charging-check
	@./build-native/charging-check

build-native/charging-check: tools/charging-check.c src/charging.c src/charging.h src/design.h \
                            $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O1 -g \
	      -o $@ tools/charging-check.c src/charging.c $(I18N_SRC) -lm

# "Getting hot" (src/heat.h): the 85 C trip, two readings in a row, the
# hysteresis under 80 C, five quiet minutes, the poll's clock. Plain C.
check-heat: build-native/heat-check
	@./build-native/heat-check

build-native/heat-check: tools/heat-check.c src/heat.c src/heat.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O1 -g \
	      -o $@ tools/heat-check.c src/heat.c

# The theme file the launcher writes and Diatom and portkeys read, so that the
# bar over a game and over a port is drawn in the theme the shelf is in: a file
# that is missing or says nothing sensible reads as Beard, the word the writer
# says reads back, and no half-written file is ever to be seen.
check-themefile: build-native/themefile-check
	@./build-native/themefile-check

build-native/themefile-check: tools/themefile-check.c src/themefile.c src/themefile.h \
                              src/design.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/themefile-check.c src/themefile.c

# What portkeys makes of the volume keys over a port (tools/pkkeys.c), where it
# steps and saves ZyperOS's own levels: MENU+VOL is the backlight whichever of
# the two devices is read first, the kernel's autorepeat is no step, a held key
# repeats on the launcher's clock (src/keyrepeat.h), and the presses that once
# saved 6 and 6 save 5 and 7 through the launcher's src/db.c. And the boost past
# the top over a port (src/boost.h): three presses or a hold open it, its gain
# goes in a scratch copy of the file the port's plugin reads (src/eqdsp.h), and
# its cells are kept beside the levels. And over your PICO-8, SELECT's
# hotkeys by Diatom's chord rule (tools/pkhot.c), the keyboard's CTRL-R
# (tools/pkkbd.c), written into a pipe, and the screenshot (tools/pkshot.c):
# PICO-8's 128 out of the panel in every Display mode's rect, the PNG, the
# pop-up; and what PICO-8 reads while portkeys holds the joypad for the
# hotkeys (a chord never, every other press at once) and that pad's writes
# (tools/pkvpad.c), into a pipe. No SDL, no device.
check-pkkeys: build-native/pkkeys-check
	@./build-native/pkkeys-check

build-native/pkkeys-check: tools/pkkeys-check.c tools/pkkeys.c tools/pkkeys.h src/keyrepeat.h \
                           src/device/pixel2_levels.h src/audioout.c src/audioout.h \
                           src/boost.h src/osdbar.h src/design.h src/eqdsp.h \
                           tools/pkhot.c tools/pkhot.h diatom/src/chord.c diatom/src/chord.h \
                           tools/pkkbd.c tools/pkkbd.h tools/pkshot.c tools/pkshot.h src/p8draw.c src/p8draw.h \
                           tools/pkvpad.c tools/pkvpad.h \
                           src/dmodefit.c src/dmodefit.h \
                           src/db.c src/db.h src/atomic.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc -Idiatom/src -Idiatom/include \
	      -o $@ tools/pkkeys-check.c tools/pkkeys.c tools/pkhot.c diatom/src/chord.c tools/pkkbd.c \
	      tools/pkshot.c tools/pkvpad.c src/p8draw.c src/dmodefit.c src/audioout.c src/db.c src/atomic.c $(I18N_SRC) \
	      -lsqlite3 -lz -lm

# Which theme is on: MENU > Appearance's three settings, Auto's seven to seven
# by the handheld's clock and the minute it may be late by, and the file the
# bar's other programs read saying the theme that is on after every kind of
# change, and the saved word as portkeys resolves it over a port, where
# nothing writes that file. No SDL, and it writes a scratch file, never
# /tmp/zyperos-theme.
check-theme: build-native/theme-check
	@./build-native/theme-check

# The player's look (src/look.h, DESIGN.md 3.5): every accent with every
# background tone, in both themes, held to the contrast DESIGN.md 3.2 owes
# each color; the look as it ships is the tokens to the byte.
check-look: build-native/look-check
	@./build-native/look-check

build-native/look-check: tools/look-check.c src/look.c src/look.h src/theme.h src/design.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/look-check.c src/look.c -lm

build-native/theme-check: tools/theme-check.c src/theme.c src/theme.h src/look.c src/look.h \
                          src/themefile.c src/themefile.h src/design.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/theme-check.c src/theme.c src/themefile.c src/look.c $(I18N_SRC) -lm

# System Update end to end (update/, docs/updates.md): two fake releases,
# packed and signed with a throwaway key, a fake card, check, download, a cut
# download resumed, refused lists and packs, and the boot switch with its
# step back. Skips without OpenSSL 3, libcrypto, libcurl or zlib.
check-update: build-native/zyperos-update
	@sh tools/update-check.sh

# What MENU > System Update says in each state (src/sysupdate.c), held to
# the update lab's screens
check-sysupdate: build-native/sysupdate-check
	@./build-native/sysupdate-check

build-native/sysupdate-check: tools/sysupdate-check.c src/sysupdate.c src/sysupdate.h src/menu.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O1 -g -o $@ tools/sysupdate-check.c src/sysupdate.c $(I18N_SRC)

# Your PICO-8 (src/pico8.c): the zip's rules, a zip copied by hand taken
# out (zips the check writes itself, never Lexaloffle's files), Splore's entry
# and the page's words; and its picture (src/p8draw.h): each Display mode
# Diatom's rect for a 128 x 128 picture, and sd/pixel2/PICO-8.sh, run with a
# stand-in pico8_64, handing that rect (-draw_rect) and filter to it
check-pico8: build-native/pico8-check
	@./build-native/pico8-check

# Your PICO-8's wget (sd/pixel2/PICO-8-wget.sh, ZyperOS/pico8/bin/wget):
# Splore's wget calls handed to curl, with HTTPS and the certificates
# checked, against a stand-in curl and system wget
check-pico8-wget:
	@sh tools/pico8-wget-check.sh

# MP4 over your PICO-8 (DESIGN.md 11.55, src/mp4hand.h), as over a Diatom
# game: the queue's file handed between processes, whole or not at all
# (tools/mp4hand-check.c, with AddressSanitizer and UBSan where the compiler
# has them); then launch.sh's run_port with stand-ins (MP4 not stopped, the
# follower beside the program and portkeys told of it, ended before the
# launcher) and the real launcher in desk mode's dummy window against a
# stand-in daemon: the queue taken on back from PICO-8, the request and the
# file written as a cart opens, the follower's next track, and SELECT in the
# in-game menu over it opening Now Playing; PICO-8 quiet while MP4 plays
# (the equalizer's file's cut, as the launcher, the follower and the menu
# write it, and the chain every port has, unchanged) and PICO-8 into a
# USB-C DAC's chain only where it opens as PICO-8 opens it (--alsa-try,
# against alsa-lib on Linux) (tools/pico8-mp4-check.py). Each wait is for
# an event, never for a time.
check-pico8-mp4: build-native/mp4hand-check native
	@./build-native/mp4hand-check
	@python3 tools/pico8-mp4-check.py

# The in-game menu's Display row over your PICO-8 (src/portmenu.h's
# port_menu_display): launch.sh's run_port with stand-ins, a start again from
# the restart file and USR2 (the program ended and started again with the
# new Display, portkeys and the follower going on, a bad word a plain Quit, a
# file from before none, POWER meanwhile starting nothing), then the real
# launcher in desk mode's dummy window as portkeys starts it: the row, its
# list, the question with the cursor on Cancel, Start Again's exit and what
# it keeps, none over OpenBOR, and --display, the Next Display hotkey's
# question alone (tools/pico8-display-check.py)
check-pico8-display: native
	@python3 tools/pico8-display-check.py

# Your PICO-8's hotkeys, Reset and buttons as the launcher and PICO-8.sh do
# them (src/portmenu.h, src/p8pad.h): PICO-8.sh's pad line against a
# stand-in pico8_64; then the real launcher in desk mode's dummy window: a
# cart opened handing portkeys the hotkeys and the screenshot's folder,
# PICO-8.sh the pad's line and the pop-up's slab with its empty square; the
# menu's Reset row and question, --reset's question alone, and Controls over
# it changing a button, with the start again it asks for
check-pico8-keys: native
	@python3 tools/pico8-keys-check.py

# MENU, then X over your PICO-8 ends it at once (1.1.0-rc.8, "has to be done
# twice"): launch.sh's run_port with stand-ins, a script that execs its
# program (PICO-8.sh, OpenBOR.sh) TERMed within a second of the menu's Quit
# and of POWER, not 2 s later, and a script that runs its program as a child
# left to go on after it, as before. Linux only (port_end reads /proc);
# elsewhere it says it skipped, and PICO8_QUIT_DOCKER=1 runs it in
# tools/p8fx-gl.Dockerfile's Linux (tools/pico8-quit-check.py)
check-pico8-quit:
	@python3 tools/pico8-quit-check.py $(if $(PICO8_QUIT_DOCKER),--docker)

# Your PICO-8's Shader and Border (src/p8fx.h), all but the drawing: the
# look file, the shaders PICO-8 is offered (the launcher's SHADERS for its
# shelf) and Next Shader on a scratch database, its picture out of PICO-8's
# preblit (build-native/p8fx-check); sd/pixel2/PICO-8.sh loading p8fx.so
# first, with a stand-in pico8_64; and the real launcher in desk mode's dummy
# window as portkeys starts it over PICO-8: the Border, Shader and Strength
# rows in the Display tab's order, each kept and written for the shim, Your
# Own's picture file, none over OpenBOR, and the look file written as a cart
# opens from the shelf (tools/p8fx-check.py)
check-p8fx: build-native/p8fx-check native
	@python3 tools/p8fx-check.py

build-native/p8fx-check: tools/p8fx-check.c src/p8fx.c src/p8fx_step.c src/p8fx.h src/strength.h src/i18n_ids.h
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O1 -g -D_GNU_SOURCE -Isrc -o $@ tools/p8fx-check.c src/p8fx.c src/p8fx_step.c -lsqlite3

# ...and the drawing: the shim (src/p8fx/shim.c) over a stand-in that calls
# SDL as pico8_64 does (tools/p8fx-standin.c), on SDL's offscreen video and
# Mesa's OpenGL ES: fail-safe to the pixel, each shader against Diatom's GLSL
# in C (shaderdemo --image), Blur and a picture round the picture, PICO-8's
# FPS line on top, a look changed while it runs. Linux only (LD_PRELOAD,
# Mesa); elsewhere it says it skipped, and P8FX_DOCKER=1 runs it in
# tools/p8fx-gl.Dockerfile's Linux (tools/p8fx-gl-check.py)
check-p8fx-gl:
	@python3 tools/p8fx-gl-check.py $(if $(P8FX_DOCKER),--docker)

build-native/mp4hand-check: tools/mp4hand-check.c src/mp4hand.c src/mp4hand.h src/mp4queue.c src/mp4queue.h \
                            src/portmenu.h src/pico8.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g $(MEDIA_SAN) -Isrc \
	      -o $@ tools/mp4hand-check.c src/mp4hand.c src/mp4queue.c -lm

build-native/pico8-check: tools/pico8-check.c src/pico8.c src/pico8.h src/sysupdate.h src/library.h \
                          src/p8draw.c src/p8draw.h src/dmodefit.c src/dmodefit.h diatom/src/scale.c \
                          sd/pixel2/PICO-8.sh $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O1 -g -Idiatom/src -Idiatom/include -o $@ tools/pico8-check.c src/pico8.c \
	      src/p8draw.c src/dmodefit.c diatom/src/scale.c $(I18N_SRC) -lz

# What the Install PortMaster page says in each state (src/pminstall.c,
# DESIGN.md 11.32), and the Ports shelf's PortMaster entry
check-pminstall: build-native/pminstall-check
	@./build-native/pminstall-check

build-native/pminstall-check: tools/pminstall-check.c src/pminstall.c src/pminstall.h src/sysupdate.c src/sysupdate.h src/library.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O1 -g -o $@ tools/pminstall-check.c src/pminstall.c src/sysupdate.c $(I18N_SRC)

# PortMaster's cover in each theme and its page's icon (res/portmaster/*.png)
# are ZyperOS's art's (res/system-art/original/utility), byte for byte
.PHONY: check-pmart
check-pmart:
	@for f in icon cover-light cover-dark; do \
		cmp -s res/portmaster/$$f.png res/system-art/original/utility/portmaster-$$f.png || \
		{ echo "check-pmart: res/portmaster/$$f.png is not res/system-art/original/utility/portmaster-$$f.png" >&2; exit 1; }; \
	done

# The PortMaster installer itself (res/portmaster/pm-install.py), on this
# computer against a made-up release served from a folder: an install, a
# second press, a checksum that does not match, a zip with a member outside
# PortMaster/, a release list that makes no sense, runtimes kept
check-pmhelper:
	@python3 -I tools/pminstall-run.py

# The libraries PortMaster's ports and their runtimes load from the system
# (plastron's board/zyperos/ports-libs.txt) asked for in the image of each
# device that runs ports, and plastron's release check of a built root
# (scripts/ports-libs.sh) over made-up roots. No image is built.
check-portlibs:
	@sh tools/portlibs-check.sh

# What a port's own files say for its Controls pages over it (src/portdoc.c:
# port.json, its gptokeyb keymap, its README's controls), and the pages they
# make (src/controls.c), on the made-up ports in tools/fixtures/ports and on
# the forms porters write by hand; PortMaster's own page. No SDL.
check-portdoc: build-native/portdoc-check
	@./build-native/portdoc-check

build-native/portdoc-check: tools/portdoc-check.c src/portdoc.c src/portdoc.h src/rajson.c src/rajson.h \
                           src/controls.c src/controls.h src/remap.c src/remap.h src/portmenu.h src/menu.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -o $@ tools/portdoc-check.c src/portdoc.c \
	      src/rajson.c src/controls.c src/hotkeys.c src/powerhold.c src/remap.c $(I18N_SRC)

# plastron's make push writes the system slot the Pixel 2 is not running,
# never the one it runs from (scripts/px2-slot.sh): the slot choice, the
# bootstate.txt edits, and the device's script parsing. Host shell only.
check-px2-push:
	@sh plastron/scripts/px2-slot-check.sh

# The Pixel 2's Wi-Fi watcher (plastron's /usr/lib/zyperos/wifi-watch): signal
# lines in wifi.log, and one retry after a drop that cut a handshake
check-wifi-watch:
	@sh tools/wifi-watch-check.sh

# launch.sh's start on a card a power cut broke (sd/zyperos/launch.sh with each
# device's sd/DEVICE/device.sh): a junk boot.env read as off, not sourced, and
# five fast stops of the launcher, or no device.sh, set launch.sh aside once,
# so the next start copies ZyperOS again; and on the Pixel 2, the kernel's
# line about the battery at the start copied into the start's log
check-launch-start:
	@sh tools/launch-start-check.sh

# The Pixel 2's crash report (plastron's /usr/sbin/crashlog): what pstore kept
# of a panic or a reset, said once, onto the card for Download logs
check-crashlog:
	@sh tools/crashlog-check.sh

# The display's gamma table, emptied (src/panelgamma.h): the launcher's and
# Diatom's code against a stand-in display, with Linux's request numbers
check-panelgamma: build-native/panelgamma-check
	@./build-native/panelgamma-check

build-native/panelgamma-check: tools/panelgamma-check.c src/panelgamma.c src/panelgamma.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O1 -g -o $@ tools/panelgamma-check.c src/panelgamma.c -lm

# The TrimUI Brick's display's gamma table and saturation
# (src/panelgamma_sunxi.h): its Screen Colors and Night Mode against a
# stand-in /dev/disp, with the disp2 driver's command numbers
check-panelgamma-sunxi: build-native/panelgamma-sunxi-check
	@./build-native/panelgamma-sunxi-check

build-native/panelgamma-sunxi-check: tools/panelgamma-sunxi-check.c src/panelgamma_sunxi.h src/panelgamma.c \
                                     src/panelgamma.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O1 -g -o $@ tools/panelgamma-sunxi-check.c src/panelgamma.c -lm

# Night Mode (src/nightmode.h): its warmth in the table, the file's nights
# and the clock against them, the nights in two zones across summer time's
# ends, and the migration from Appearance's Auto on a scratch database
check-nightmode: build-native/nightmode-check
	@./build-native/nightmode-check

build-native/nightmode-check: tools/nightmode-check.c src/nightmode.c src/nightmode.h src/nightkeep.c \
                              src/nightkeep.h src/panelgamma.c src/panelgamma.h src/db.c src/db.h \
                              src/atomic.c src/theme.c src/theme.h src/themefile.c src/look.c \
                              $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O1 -g -o $@ tools/nightmode-check.c \
	      src/nightmode.c src/nightkeep.c src/panelgamma.c src/db.c src/atomic.c \
	      src/theme.c src/themefile.c src/look.c $(I18N_SRC) -lsqlite3 -lpthread -lm

# Screen Colors in a game (DESIGN.md 11.42, 1.1.0): the real launcher in desk
# mode's dummy window, as the Pixel 2 and as the Brick, started back into a
# game against a Diatom of the check's own, the menu up: the Display tab's
# Screen Colors row, A opening MENU's page over the paused game, a step kept
# and on the panel (desk mode's stand-in for the table) with the game still
# paused, B back to the menu and B again to the game; no row without a table
check-ingame-colors: native
	@python3 tools/ingame-colors-check.py

# The display taken back after a game, a crashed one too (src/displayback.h):
# master asked for until a dying Diatom lets go, and a CRTC left with nothing
# on it given a black picture, against a stand-in display and clock
check-displayback: build-native/displayback-check
	@./build-native/displayback-check

build-native/displayback-check: tools/displayback-check.c src/displayback.c src/displayback.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O1 -g -o $@ tools/displayback-check.c src/displayback.c

# Appearance's Auto on Night Mode's Night Hours (src/theme.h, 1.1.0): light
# in the day and dark in the night, across midnight and to the minute, dark
# at exactly the minutes Night Mode's nights are on, the row's three, the
# first start's migration once a card (a 1.0.5 Auto kept, a new Auto left
# alone), and the boot screen's word with the hours, which plastron's splash
# reads to the launcher's theme every minute of the day; and that it never
# conflicts with Night Mode: no night.* key written, Night Mode the same under
# all three, the theme turning on the warmth's own second, a 1.0.6 card's
# Night Mode settings untouched
check-autotheme: build-native/autotheme-check
	@./build-native/autotheme-check

build-native/autotheme-check: tools/autotheme-check.c src/theme.c src/theme.h src/nightmode.c src/nightmode.h \
                              src/nightkeep.c src/nightkeep.h src/panelgamma.c src/panelgamma.h src/db.c \
                              src/db.h src/atomic.c src/themefile.c src/look.c src/boottheme.c src/boottheme.h \
                              plastron/buildroot/package/splash/src/zytheme.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O1 -g -o $@ tools/autotheme-check.c \
	      src/theme.c src/nightmode.c src/nightkeep.c src/panelgamma.c src/db.c src/atomic.c \
	      src/themefile.c src/look.c src/boottheme.c $(I18N_SRC) -lsqlite3 -lpthread -lm

# The boot screen's theme words (src/boottheme.h, DESIGN.md 11.21)
check-boottheme: build-native/boottheme-check
	@./build-native/boottheme-check

build-native/boottheme-check: tools/boottheme-check.c src/boottheme.c src/boottheme.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O1 -g -o $@ tools/boottheme-check.c src/boottheme.c

# The boot screens at the player's brightness (src/boottheme.h): the word
# plastron's splash lights the panel at (package/splash/src/bright.h), and
# its init's bright_keep, which sets the backlight from the card as soon as
# the card is mounted, run on a scratch card and scratch backlights.
check-splashbright: build-native/splashbright-check
	@./build-native/splashbright-check

build-native/splashbright-check: tools/splashbright-check.c plastron/buildroot/package/splash/src/bright.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O1 -g -o $@ tools/splashbright-check.c

check-bootbright:
	@sh tools/bootbright-check.sh

# The boot's ZyperOS version compare (plastron's zyperos.init, version_of and
# newer_seed): a version with a suffix (1.1.0-rc.1, -kit.N, -test.N) is a
# version, below its release, so an RC or kit pushed over an older card puts
# its ZyperOS folder there; what is not a version is false and never stops
# the shell
check-seed-version:
	@sh tools/seed-version-check.sh

UPDATE_SRC = update/main.c update/manifest.c update/status.c update/slots.c
build-native/zyperos-update: $(UPDATE_SRC) update/update.h FORCE
	@mkdir -p build-native
	@if pkg-config --exists libcrypto libcurl zlib 2>/dev/null; then \
		$(CC) -std=gnu11 -Wall -Wextra -Wno-unused-parameter -O1 -g -D_GNU_SOURCE -DZYPEROS_VERSION='"check"' \
		      $$(pkg-config --cflags libcrypto libcurl zlib) -o $@ $(UPDATE_SRC) \
		      $$(pkg-config --libs libcrypto libcurl zlib); \
	else printf '#!/bin/sh\nexit 0\n' > $@; chmod +x $@; fi

# What portkeys reads to draw the bar in the launcher's theme over a port
# (tools/pkset.c): the Appearance word from the device database, the zone from
# the card's library (one on Date & Time's list, else New York), and the theme
# file only when the database cannot be read - against scratch databases that
# are missing, empty, not databases or locked. SQLite, no SDL, no device.
# MENU+START's chord (tools/zychord.h): a second's hold, either order, once
# per hold, nothing for a tap of both
check-zyhome: build-native/zyhome-check
	@./build-native/zyhome-check

build-native/zyhome-check: tools/zyhome-check.c tools/zychord.h
	@mkdir -p build-native
	@$(CC) -Wall -Wextra -Werror -std=gnu11 -o $@ tools/zyhome-check.c

# portkeys's picture on the cursor window (tools/pkover.h): the bar alone and
# the heat notice alone where and as they went before, the low-battery disc
# Diatom's to the pixel and its blink, On-Screen Stats' line where hud_place
# puts it, clear of the disc, all of them in one buffer turned and not, and
# the frames a second counted from the framebuffer at each blank. No DRM.
check-pkover: build-native/pkover-check
	@./build-native/pkover-check

build-native/pkover-check: tools/pkover-check.c tools/pkover.c tools/pkover.h src/hud.c src/hud.h \
                           src/hudglyphs.h src/osdbar.c src/osdbar.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -o $@ tools/pkover-check.c tools/pkover.c src/hud.c src/osdbar.c -lm

check-portkeys: build-native/portkeys-check
	@./build-native/portkeys-check

build-native/portkeys-check: tools/portkeys-check.c tools/pkset.c tools/pkset.h \
                             src/theme.c src/theme.h src/themefile.c src/themefile.h \
                             src/clock.c src/clock.h src/design.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -o $@ tools/portkeys-check.c tools/pkset.c src/theme.c src/themefile.c src/look.c \
	      src/clock.c $(I18N_SRC) -lsqlite3 -lm

# systems.cfg's accent field: six hex digits are that color, and anything else
# - an empty field, a line that stops before it, a word - is no color at all
# (CFG_NO_COLOR), which the screens draw by role and never as a color some
# system has. And its es lines: the folders other systems keep games in,
# ROCKNIX's all, each read on its own shelf. No SDL; one scratch file under a
# directory of its own.
check-config: build-native/config-check
	@./build-native/config-check

build-native/config-check: tools/config-check.c src/config.c src/config.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/config-check.c src/config.c

# A shelf's firmware as its core judges it (src/biosfile.h): every disc bios
# name in config/systems.cfg has its size, and a 0-byte, short or long file in
# Bios/ is refused with its name, in the core's order (a bad saturn_bios.bin
# beside a good mpr-17933.bin), while a right-size one starts. No SDL; files
# under a directory of its own.
check-bios: build-native/bios-check
	@./build-native/bios-check

build-native/bios-check: tools/bios-check.c src/biosfile.c src/biosfile.h src/config.c src/config.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/bios-check.c src/biosfile.c src/config.c $(I18N_SRC)

# A game's own file as the shelf judges it before its core (src/gamefile.h):
# a 0-byte file of any kind refused as empty (a 0-byte PSP .iso crashed
# PPSSPP), a zip, 7z, CHD, CSO, PBP or iso under its kind's least size refused
# as incomplete, a ROM, cue or m3u judged by being empty alone, and what the
# shelf says. No SDL; files under a directory of its own.
check-gamefile: build-native/gamefile-check
	@./build-native/gamefile-check

build-native/gamefile-check: tools/gamefile-check.c src/gamefile.c src/gamefile.h src/biosfile.c src/biosfile.h src/config.c src/config.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/gamefile-check.c src/gamefile.c src/biosfile.c src/config.c $(I18N_SRC)

# What the shelf says when a core would not load (src/startfail.h): another
# machine's build, a library it needs, a library too old, a function nothing
# has, or nothing more known, in each of the eight languages, with the core's
# file name and the name dlerror gave, cut where it is long. No SDL.
check-startfail: build-native/startfail-check
	@./build-native/startfail-check

build-native/startfail-check: tools/startfail-check.c src/startfail.c src/startfail.h src/platform.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/startfail-check.c src/startfail.c $(I18N_SRC)

# The on-screen keyboard's d-pad: down from the grid onto the action row under
# a column, up from an action key onto the column under its center, the wraps
# at every edge, and the action keys' labels on each layer. No SDL.
check-kbnav: build-native/kbnav-check
	@./build-native/kbnav-check

build-native/kbnav-check: tools/kbnav-check.c src/kbnav.c src/kbnav.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/kbnav-check.c src/kbnav.c $(I18N_SRC)

# The menus' focus plate and the give of their list, a move in quick on the
# out curve (src/tween.c): a press mid-move takes it on from where it is, and
# held, it is on its row before the d-pad's next repeat - REPEAT_RATE_MS, read
# from src/keyrepeat.h. No SDL.
check-tween: build-native/tween-check
	@./build-native/tween-check

build-native/tween-check: tools/tween-check.c src/tween.c src/tween.h src/design.h \
                          src/keyrepeat.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -DREPEAT_RATE_MS=$$(sed -n 's/^#define REPEAT_RATE_MS[^0-9]*\([0-9][0-9]*\).*/\1/p' src/keyrepeat.h) \
	      -o $@ tools/tween-check.c src/tween.c -lm

# Result holds and the real input loop, with a fake clock and no SDL.
# The source guard also keeps all 13 result sites off blocking delays.
check-messages: build-native/message-check
	@./build-native/message-check
	@python3 tools/messages-check.py

build-native/message-check: tools/message-check.c src/message.c src/message.h src/design.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O1 -g -o $@ tools/message-check.c src/message.c

# A menu's panel opening and closing (src/menumotion.c): in quick on out
# from 96% with its scrim, out in exit on in, one panel at a time over a
# submenu and back, a press that puts it at rest - and that, on a screen
# still waiting for the panel before it, makes its own the one that leaves
# next - and a held d-pad that starts nothing over (REPEAT_RATE_MS, read from
# src/keyrepeat.h), MENU deep in a submenu unwinding every level with the
# panel on screen leaving once, and MENU in MP4's menu bringing back what is
# under it; and, read from src/main.c with its comments taken out, the
# in-game menu's close reading input, ended by a press, stopping the game on
# power, drawn before RESUME and nothing after. No SDL.
check-menumotion: build-native/menumotion-check
	@./build-native/menumotion-check

build-native/menumotion-check: tools/menumotion-check.c src/menumotion.c src/menumotion.h \
                               src/design.h src/keyrepeat.h src/main.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -DREPEAT_RATE_MS=$$(sed -n 's/^#define REPEAT_RATE_MS[^0-9]*\([0-9][0-9]*\).*/\1/p' src/keyrepeat.h) \
	      -o $@ tools/menumotion-check.c src/menumotion.c -lm

# A menu's panel kept whole (src/menusnap.c), for the next screen to draw it
# leaving: every row's text copied, the row marks and a rule's NULL kept as
# they are, Zyper's rig by value, and nothing past MENUSNAP_ROWS rows or
# MENUSNAP_TEXT bytes. No SDL.
check-menusnap: build-native/menusnap-check
	@./build-native/menusnap-check

build-native/menusnap-check: tools/menusnap-check.c src/menusnap.c src/menusnap.h \
                             src/menu.h src/menu.c src/zyperrig.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/menusnap-check.c src/menusnap.c src/menu.c

# The card drawn where there is no art, in each theme: its title reads on
# every row of its slab in Beard and in Salt & Pepper, and a card made for one
# theme is never shown in the other. No SDL.
check-focusfx: build-native/focusfx-check
	@./build-native/focusfx-check

build-native/focusfx-check: tools/focusfx-check.c src/focusfx.c src/focusfx.h src/design.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/focusfx-check.c src/focusfx.c -lm

check-cardart: build-native/cardart-check
	@./build-native/cardart-check

build-native/cardart-check: tools/cardart-check.c src/cardart.c src/cardart.h config/systems.cfg FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/cardart-check.c src/cardart.c -lm

check-cardgen: build-native/cardgen-check
	@./build-native/cardgen-check

build-native/cardgen-check: tools/cardgen-check.c src/cardgen.c src/cardgen.h \
                            src/design.h src/theme.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/cardgen-check.c src/cardgen.c src/look.c -lm

# Beard's shadows: a pass of the box blur keeps its light, three passes reach
# exactly as far as a shadow's texture is padded, the nine pieces it is drawn
# in under a rect of any size cover it once and, under a big one, are that
# rect's own shadow with no seam where a corner meets an edge, and the texture
# made at half size and drawn back up with linear filtering is the shadow made
# at full size; and a shape of triangles, Zyper's for his shadow, fills as the
# union of them with no seam. No SDL.
check-blur: build-native/blur-check
	@./build-native/blur-check

# The grid's rule (src/gridrule.h, DESIGN.md 11.47): the shape a shelf takes
# from its art, where the cells go, which art is a cut-out
check-gridrule: build-native/gridrule-check
	@./build-native/gridrule-check

build-native/gridrule-check: tools/gridrule-check.c src/gridrule.c src/gridrule.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g $$(pkg-config --cflags sdl2) -Isrc \
	      -o $@ tools/gridrule-check.c src/gridrule.c $$(pkg-config --libs sdl2) -lm

# Recents' Last Screen card (src/screencard.h, DESIGN.md 11.48): the
# picture whole times toward its height, the cover in its corner over its
# shadow, a wide box held to its share
check-screencard: build-native/screencard-check
	@./build-native/screencard-check

build-native/screencard-check: tools/screencard-check.c src/screencard.c src/screencard.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g $$(pkg-config --cflags sdl2 SDL2_image SDL2_ttf) -Isrc \
	      -o $@ tools/screencard-check.c src/screencard.c $$(pkg-config --libs sdl2 SDL2_image) -lm

# The focus's marks on whole panel pixels (src/focusgeo.h, DESIGN.md 11.47):
# each mark's edges against the Pixel 2's linear scaling, plain C
check-focusgeo: build-native/focusgeo-check
	@./build-native/focusgeo-check

build-native/focusgeo-check: tools/focusgeo-check.c src/focusgeo.c src/focusgeo.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O1 -g -o $@ tools/focusgeo-check.c src/focusgeo.c -lm

check-emptyhome: build-native/emptyhome-check
	@./build-native/emptyhome-check

build-native/emptyhome-check: tools/emptyhome-check.c src/emptyhome.c src/emptyhome.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g $$(pkg-config --cflags sdl2) -Isrc \
	      -o $@ tools/emptyhome-check.c src/emptyhome.c $(I18N_SRC)

check-roundrect: build-native/roundrect-check
	@./build-native/roundrect-check | tail -1

build-native/roundrect-check: tools/roundrect-check.c src/roundrect.c src/roundrect.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/roundrect-check.c src/roundrect.c -lm

check-rowmacro:
	@python3 tools/rowmacro-check.py

build-native/blur-check: tools/blur-check.c src/blur.c src/blur.h src/design.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/blur-check.c src/blur.c -lm

# Zyper's moves beside the ZyperOS menu's heading (src/zyperrig.c): still
# at rest but for his breath and a blink every 3 to 6 s; the hop the menu
# opens with, the hop and pant of a success and the cocked head of a failure,
# at their times; a reaction that starts from where he is, a press that drops
# what was booked, a move that does nothing, and a blink on its own time. No
# SDL.
check-zyperrig: build-native/zyperrig-check
	@./build-native/zyperrig-check

build-native/zyperrig-check: tools/zyperrig-check.c src/zyperrig.c src/zyperrig.h \
                             src/design.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/zyperrig-check.c src/zyperrig.c -lm

# Wi-Fi File Transfer's hotspot, its pure half (src/captive.c): the DNS answer
# that sends every name to the handheld, the Host check that tells a phone's
# internet check from the page, and the QR code's text and the password.
check-captive: build-native/captive-check
	@./build-native/captive-check

build-native/captive-check: tools/captive-check.c src/captive.c src/captive.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/captive-check.c src/captive.c

# zyperos.local (src/mdns.h): the queries a Mac sent, read off the wire, each
# answered as RFC 6762 says (QU, the group, a one-shot resolver, the NSEC
# beside the A, known answers kept back); every piece of them and random
# bytes, under the address sanitizer where the compiler has it; and
# responders on a pretend network, where two handhelds on one Wi-Fi end with
# two names.
check-mdns: build-native/mdns-check
	@./build-native/mdns-check

build-native/mdns-check: tools/mdns-check.c src/mdns.c src/mdns.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g $(MEDIA_SAN) \
	      -o $@ tools/mdns-check.c src/mdns.c

# File Transfer's link steps (src/linksteps.h): the cable, the address and
# the browser, each hint at its time, a replug, Wi-Fi, and the device's
# /proc/net/arp and UDC state as they read.
check-linksteps: build-native/linksteps-check
	@./build-native/linksteps-check

build-native/linksteps-check: tools/linksteps-check.c src/linksteps.c src/linksteps.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/linksteps-check.c src/linksteps.c

# plastron's usbdrive (the card as a USB drive): never exported while
# mounted or held, and how an export ends. Host compiler only, no device.
check-usbdrive:
	@mkdir -p build-native && $(CC) -std=c11 -Wall -Wextra -Werror -O1 $(MEDIA_SAN) \
		-o build-native/usbdrive-check plastron/buildroot/package/usbdrive/tests/drive-check.c \
		plastron/buildroot/package/usbdrive/src/drive.c plastron/buildroot/package/usbdrive/src/drivestat.c \
		plastron/buildroot/package/usbdrive/src/atlas.c && build-native/usbdrive-check

# plastron's gauge (the Pixel 2's side lights): every rule of what they
# show, the battery's parts, the launcher's patterns and the wave while files
# move (package/gauge/src/lights.c), as plastron's own `make gauge-check`
# holds them. Host compiler only, no device.
check-gauge:
	@mkdir -p build-native && $(CC) -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O1 \
		-o build-native/gauge-check plastron/buildroot/package/gauge/src/check.c \
		plastron/buildroot/package/gauge/src/lights.c && build-native/gauge-check

# The GKD Pixel 2's battery level at a start (plastron's kernel patch
# 9-px2-rk817-start-soc.patch): the voltage at rest, with the current's drop
# taken out, and where a start's level comes from, as plastron's own `make
# start-soc-check` holds them against the Pixel 2's device tree. Host
# compiler only, no device.
check-start-soc:
	@$(MAKE) --no-print-directory -C plastron start-soc-check

# When the Pixel 2's USB port may be a host (plastron's usbrole, its
# src/rule.c): the nine plugs of a Mac traced on 2026-11-10 never host, and a
# DAC only on USB Accessory for its own plug; never with power in, a computer's
# power or a failed reading. Host compiler only, no device.
check-usbrole:
	@mkdir -p build-native && cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O1 $(MEDIA_SAN) \
		-o build-native/usbrole-rule-check plastron/buildroot/package/usbrole/tests/rule-check.c \
		plastron/buildroot/package/usbrole/src/rule.c && build-native/usbrole-rule-check

# USB Device Connected, the launcher's half of usbrole (src/usbpower.h):
# usbrole's state lines, a request written whole and never through a link,
# and the question asked once per plug, after 2 s, never for a computer's.
# The USB port's log for support (src/usblog.h): a plug a line, the last 50
check-usblog:
	@mkdir -p build-native && $(CC) -std=gnu11 -Wall -Wextra -Werror -O1 $(MEDIA_SAN) -Isrc \
		-o build-native/usblog-check tools/usblog-check.c src/usblog.c src/usbpower.c && build-native/usblog-check

# A phone's USB tethering (src/phonelink.h): its interface told from the
# gadget's own usb0, an iPhone told from a Mac, File Transfer once a link
check-phonelink: build-native/phonelink-check
	@./build-native/phonelink-check

build-native/phonelink-check: tools/phonelink-check.c src/phonelink.c src/phonelink.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g $(MEDIA_SAN) \
	      -o $@ tools/phonelink-check.c src/phonelink.c

check-usbpower: build-native/usbpower-check
	@./build-native/usbpower-check

build-native/usbpower-check: tools/usbpower-check.c src/usbpower.c src/usbpower.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g $(MEDIA_SAN) \
	      -o $@ tools/usbpower-check.c src/usbpower.c

# File Transfer's "1 connected" (src/clients.h): browsers, not sockets: one
# tab with five keep-alive connections is one, two browsers are two.
check-clients: build-native/clients-check
	@./build-native/clients-check

build-native/clients-check: tools/clients-check.c src/clients.c src/clients.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O1 -g $(MEDIA_SAN) -o $@ tools/clients-check.c src/clients.c

# Button hints (src/keyhints.h), the one component that says what each
# button does: rows in order and in their room, a list packed into a panel's
# row and read back in all eight languages.
# When a panel screen draws (src/pace.h): its heartbeat while still, every
# pass while anything moves, the memory idle after 2 s
check-pace: build-native/pace-check
	@./build-native/pace-check

build-native/pace-check: tools/pace-check.c src/pace.c src/pace.h src/menu.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O1 -g $(MEDIA_SAN) -Isrc -o $@ tools/pace-check.c src/pace.c

check-keyhints: build-native/keyhints-check
	@./build-native/keyhints-check

build-native/keyhints-check: tools/keyhints-check.c src/keyhints.c src/keyhints.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g $(MEDIA_SAN) \
	      -o $@ tools/keyhints-check.c src/keyhints.c $(I18N_SRC)

# The hotspot's QR code (src/qr.c), read back by zbarimg where the host has it.
check-qr: build-native/qr-check
	@./build-native/qr-check

build-native/qr-check: tools/qr-check.c src/qr.c src/qr.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/qr-check.c src/qr.c

# A name too wide for its box, broken as the port loading screen breaks it:
# one line when it fits, else the fewest lines that fit and of those the break
# whose widest line is narrowest, held to a search of every break; never more
# than the cap or a line over the box, and a word too wide for it cut as
# ui_fit_text cuts one. And where a marquee's window ends at rest: between two
# letters, never through one or through the bytes of one. No SDL.
check-linebreak: build-native/linebreak-check
	@./build-native/linebreak-check

build-native/linebreak-check: tools/linebreak-check.c src/linebreak.c src/linebreak.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/linebreak-check.c src/linebreak.c

# The tutorial's steps (DESIGN.md 11.10): what each launcher button is to it
# and which glyph part the hand lights, every step in order and what each
# waits for, the deaf start of a step for every key, START, the wrong presses
# and what Zyper says to each, every button's name, POWER only in its own
# step, the volume keys, the reward's hold, the demonstration loops' clocks,
# Zyper's moves (the rig's, the corner's rise, the hello's pop), the little
# shelf's colors against config/systems.cfg, and tour.done saved only for a
# tour that is seen. No SDL.
check-tour: build-native/tour-check
	@./build-native/tour-check

build-native/tour-check: tools/tour-check.c src/tour.c src/tour.h src/design.h \
                         src/inbutton.h src/zyperrig.c src/zyperrig.h src/config.c \
                         src/config.h src/db.c src/db.h src/atomic.c src/atomic.h \
                         $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/tour-check.c src/tour.c src/powerhold.c src/zyperrig.c src/config.c src/db.c \
	      src/atomic.c $(I18N_SRC) $(DEVICE_PROFILES) -lsqlite3 -lm

# A system's color as a mark (DESIGN.md 3.3): as systems.cfg has it on Salt &
# Pepper and a quarter toward text on Beard, for every shipped system; no
# color is text-dim and lime is accent-ink, in both themes. No SDL.
check-sysmark: build-native/sysmark-check
	@./build-native/sysmark-check config/systems.cfg

build-native/sysmark-check: tools/sysmark-check.c src/sysmark.c src/sysmark.h \
                            src/config.c src/config.h src/design.h src/theme.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/sysmark-check.c src/sysmark.c src/config.c src/look.c -lm

# A change of theme leaves nothing drawn for the old one on the systems row,
# the games shelf or Now Playing's cover, nor Plain Jane's art of the old one
# on the systems row: each, started in one theme, drawn, and turned to the
# other the way Auto turns it, is the same picture, byte for byte, as when
# started in the other. The host build's --shot, so it needs SDL2, and skips
# cleanly without it.
check-flip:
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@sh tools/flip-check.sh

# A menu's open, close and swap at their ends, as the launcher draws them:
# the open from the bare shelf to the menu at rest, the close from the menu at
# rest to the bare shelf, the swap onto Controls at rest, byte for byte, and
# another picture halfway through each (--menu --open/--close/--swap
# --shot-at). The host build's --shot, so it needs SDL2, and skips cleanly
# without it.
check-menushots:
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@sh tools/menushots-check.sh

# The port loading screen over a white cover and a black one, in both themes,
# and a Nintendo DS game, whose silver is pale on Beard: every line of words
# at 4.5:1 over the blurred cover, the scrim raised for them, and the system's
# band at 3:1, its color moved toward text for it, as the screen reads them
# back; and neither the words nor the cover in Zyper's frame. The host build's
# --portload, so it needs SDL2, and skips cleanly without it, as check-flip
# does.
check-portload:
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@sh tools/portload-check.sh

# The port's latest output at the foot of that screen, as plastron's splash
# draws it (splash/src/portout.h): read as a terminal shows it, drawn over the
# screen's own box, cut with an ellipsis, turned as the Pixel 2's panel is;
# and the letters and the box the launcher writes for it (port_out_write), in
# English and German, both themes, on the foot under Zyper's frame. The
# letters need the host build, and are skipped cleanly without SDL2.
# `sh tools/portout-check.sh --shots DIR` shoots the screen as the lines of a
# stand-in port change.
check-portout: build-native/portout-check
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@sh tools/portout-check.sh

build-native/portout-check: tools/portout-check.c plastron/buildroot/package/splash/src/portout.h
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -o $@ tools/portout-check.c

# Zyper as the launcher draws him (src/main.c): in the top ZyperOS menu and
# no other, his tongue in after a failure, his eyes shut in a blink, and his
# shadow on Beard - under his chin in the menu and on the power-off screen,
# going out with him - and never on Salt & Pepper; the host build's --menu
# --zyper and --poweroff shots, read back by tools/zyper-check.py with
# Python's standard library. Needs SDL2, and skips cleanly without it.
check-zyper:
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@python3 tools/zyper-check.py

# The tutorial as the launcher draws it: Zyper only for the reward and a
# wrong press and one at a time (the power step's), the check disc, his bubble
# clear of "START: skip", in both themes; on the Pixel 2 the handheld beside
# each lesson with its key lit on its side, the lesson block moved right and
# otherwise the Brick's; the choices after the lessons ringing and naming the
# focused one, checking the kept one, with their previews, the Pixel 2's the
# Brick's; and the first start showing it on a
# fresh database and not with tour.done set. Host shots read back with
# Python's standard library (builds the host launcher; skips without SDL2).
check-tour-shots:
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@python3 tools/tour-shot-check.py

# The computer-side importers (--meta, --titles) on a scratch card: a title
# written over a game's details keeps them. The host build's, so it needs
# SDL2 and skips cleanly without it.
check-importers:
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@sh tools/importer-check.sh

# The shelf's status corner keeps its place: the widest one, 00:00 in 24-hour
# with all three marks, clear of the focused card in each direction, with a
# long title and every tall panel (the ZyperOS menu both ways, a Nintendo DS
# game's menu, the achievements) a grid step under it, in both themes, as
# each shot checks its own frame; and MENU > Clock turns over and is saved.
# The host build's --shot, so it needs SDL2 and skips cleanly without it.
check-corner:
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@sh tools/corner-check.sh

# Compile the current reader/loop statements with fake input syscalls and
# hardware output. No copied loop and no device access; its files go in a
# temp folder of its own.
check-portkeys-flow:
	@CC="$(CC)" python3 tools/portkeys-flow-check.py

# The design tokens: DESIGN.md's tables, src/design.h, the mock kit's
# tokens.js and Over The Hare's colors in res/web/app.css (and the play page's
# in res/web/play/play.css) are all written by
# tools/designdef.py, and every color meets the contrast DESIGN.md owes it.
# `make design` writes them; this only compares.
check-design:
	@python3 tools/designdef.py --check

# Every string a player reads, in English, 简体中文, Español, Português
# (Brasil) and Français: res/strings/*.txt, the source, and tools/i18n.py,
# which writes src/i18n_ids.h, src/i18n_tables.c (the build's own, see
# I18N_SRC) and the File Transfer page's res/web/i18n/. The check holds the
# committed ones fresh and the tables uncommitted, every string in all five with
# English's printf conversions, every ID defined where it is used and used
# where it is defined, and every character in the font that draws it.
strings:
	python3 tools/i18n.py

# The device profiles' data (src/device/profile.h), for checks that pose a
# device; src/profile.c is linked only where one is chosen (check-devices)
DEVICE_PROFILES := src/device/pixel2_profile.c src/device/brick_profile.c src/device/desktop_profile.c \
                   src/device/td_model.c src/device/pixel2_model.c src/device/brick_model.c \
                   src/device/pixel2_coreopts.c src/device/brick_coreopts.c
# Each device's build and card (src/device/build.h): the generator and the
# checks link them; the launcher never does
DEVICE_BUILDS := src/device/pixel2_build.c src/device/brick_build.c
# The RetroAchievements hasher and the disc readers under it (src/rahash.h,
# src/discimg.h): every check that links one links them all, and liblzma
# for a CHD's LZMA hunks (Homebrew's xz on a Mac; the device has the
# system's, which FFmpeg already needs)
RAHASH_SRC := src/rahash.c src/discimg.c src/chd.c src/flacdec.c
LZMA_CFLAGS := $(shell pkg-config --cflags liblzma 2>/dev/null)
LZMA_LIBS := $(shell pkg-config --libs liblzma 2>/dev/null || echo -llzma)

check-strings:
	@python3 tools/i18n.py --check

# The launcher's main screens in each language and the pseudo-locale, from the
# host build, as the Pixel 2 and as the Brick (tools/i18n-shots.py, DEVICES):
# fails where a string from res/strings is cut, broken past its lines or drawn
# off the screen in any language on either handheld (src/ui.c says so on
# stderr; the Brick's MENU is a word, wider than the Pixel 2's three dots),
# and lists what the pseudo-locale's longer words do not fit. A row the Brick
# has not (Strength, with no GPU shaders) is said, not failed.
check-i18n-shots:
	@python3 tools/i18n-shots.py

# How the Brick's initramfs (plastron's brick-rootswitch) reads the kernel's
# line: the slot's root the boot script names, and a developer's root update
check-rootswitch:
	@mkdir -p build-native
	@$(CC) -std=gnu11 -Wall -Wextra -O1 -g -Iplastron/buildroot/package/brick-rootswitch/src \
	      -o build-native/rootswitch-check tools/rootswitch-check.c
	@./build-native/rootswitch-check

# Where the Brick's first start puts the games partition (its datapart): after
# root B, partition 5, never over what a card of another layout holds
check-datapart:
	@sh tools/datapart-check.sh

# The Brick's start checking its closed files against its image's lists: a
# card without them all can't start ZyperOS, and the splash says so
check-brick-closed:
	@sh tools/closedcheck-check.sh

# The log pack's hardware report (src/hwreport.c): its lines from a device's
# facts, and the readers of the kernel's text they come from
check-hwreport:
	@mkdir -p build-native
	@$(CC) -std=gnu11 -Wall -Wextra -O1 -g -o build-native/hwreport-check tools/hwreport-check.c src/hwreport.c
	@./build-native/hwreport-check

# A test kit's code reaches a kit alone (src/kit.h, tools/kitbuild-check.sh):
# mk/cross.mk adds ZYPEROS_KIT_BUILD and the device's kit file only for a
# VERSION with "-kit", the hooks' objects have none of it without the define
# and all of it with it, and the host launcher, never a kit, has none
check-kitbuild:
	@CC='$(CC)' DEVICE_PROFILES='$(DEVICE_PROFILES)' DEVICE_BUILDS='$(DEVICE_BUILDS)' sh tools/kitbuild-check.sh

# The Brick kit's lights walk (src/device/brick_kit_walk.h), on a hand clock:
# the LEDs in the kernel's order by number, the 5 V on first and off last,
# each LED alone for its second, writes 40 ms apart and one a pass, a stop
# turning off what is lit, and the row's words as it goes
check-kitwalk:
	@mkdir -p build-native
	@$(CC) -std=gnu11 -Wall -Wextra -Werror -O1 -g -o build-native/kitwalk-check tools/kitwalk-check.c
	@./build-native/kitwalk-check

# What the tools left in the temporary folder and the worktrees' gate frames
# that nothing touched for a day (tools/clean-scratch.py; -n lists only):
# scratch cards of runs stopped hard, about 250 MB a language
.PHONY: clean-scratch
clean-scratch:
	@python3 tools/clean-scratch.py $(SCRATCH)

# The same pixels, proved (tools/pixel-gate.py): every shot of check-i18n-shots
# in every language and both themes, at BASE (origin/main) and here, compared
# as decoded pixels, with sheets of what changed. Not in CHECKS: it builds the
# launcher twice. One language or theme: make pixel-gate GATE='--lang en'; a
# pull request's round, only what the change can touch: GATE=--targeted. The
# base's frames are shared by every worktree (the tool says how);
# pixel-gate-warm shoots BASE's and nothing else, once a main commit.
BASE ?= origin/main
.PHONY: pixel-gate pixel-gate-warm
pixel-gate:
	@'$(PYIMG)' tools/pixel-gate.py --base '$(BASE)' $(GATE)
pixel-gate-warm:
	@'$(PYIMG)' tools/pixel-gate.py --base '$(BASE)' --warm $(GATE)

# The Chinese UI's cut of Noto Sans SC (tools/cjkfont.py), from the two OTFs
# in CJK_SRC; needs fontTools. Only when check-strings says a character is
# missing from it.
cjk-font:
	@[ -n "$(CJK_SRC)" ] || { echo "cjk-font: CJK_SRC=DIR, the folder of NotoSansSC-Regular.otf and -Bold.otf" >&2; exit 1; }
	python3 tools/cjkfont.py $(CJK_SRC)

design:
	python3 tools/designdef.py

# The interface sounds' rule (src/uisound.c): which press plays what, where
# they may be heard - never on a Bluetooth headset - the setting's four
# positions, the WAVs the launcher reads, and the mix. Plain C, no SDL.
check-uisound: build-native/uisound-check
	@./build-native/uisound-check

build-native/uisound-check: tools/uisound-check.c src/uisound.c src/uisound.h \
                           src/audioout.c src/audioout.h src/design.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/uisound-check.c src/uisound.c src/audioout.c $(I18N_SRC)

# Menu music's rules (src/bgm.h): the tracks, from Menu Music and its folders
# and ZyperOS's own, the settings, the shuffle, the history L2 goes
# back along, the gain, the Menu Music screen's rows, a track's name from its
# tags (src/bgm_tags.h) and Zyper's pop-up that says it (src/songpop.h), and
# MP4's track in it when L2/R2 move MP4. No SDL and no ALSA.
check-bgm: build-native/bgm-check
	@./build-native/bgm-check

build-native/bgm-check: tools/bgm-check.c src/bgm.c src/bgm.h src/bgm_menu.c \
                        src/bgm_menu.h src/bgm_tags.c src/bgm_tags.h src/songpop.c \
                        src/songpop.h src/design.h src/menu.h src/bgm_dec.c src/bgm_ogg.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -o $@ tools/bgm-check.c src/bgm.c src/bgm_menu.c src/bgm_tags.c src/songpop.c \
	      src/bgm_dec.c src/bgm_ogg.c $(I18N_SRC) -lm

# ZyperOS's own menu music, the tracks a card with an empty Roms/BGM plays:
# every file in res/bgm/ is the one res/bgm/tracks.json records, from a
# source it names by checksum, under a license a ZyperOS card may ship (CC0
# or CC BY), credited in THIRD-PARTY-LICENSES.md and the README, Ogg Vorbis
# at the mix's 48 kHz, and all of them under the size cap. Python alone, so
# it runs where the encoder does not; tools/bgm-tracks.py encode makes them.
check-bgm-tracks:
	@python3 -B tools/bgm-tracks.py --check

# The sounds themselves: every WAV in res/sounds/ is what tools/gensounds.py
# synthesizes from the tones in tools/designdef.py, and there is no other.
# Standard library only, so it needs nothing check-design does not.
check-sounds:
	@python3 -B tools/gensounds.py --check

sounds:
	python3 -B tools/gensounds.py

# Python with Pillow, for the Plain Jane cards and their check. The first
# python3 on the PATH may have none (Homebrew's does not), so this is the first
# of it and pyenv's whose Pillow has Raqm (libraqm), for text image generation
# and the cards' names are drawn with; failing that, the first that can import
# PIL for image compilers and screenshot checks. PYIMG selects another Python.
PY_CANDIDATES := python3 $(wildcard $(HOME)/.pyenv/shims/python3) $(wildcard /usr/bin/python3)
PY_RAQM = $(foreach p,$(PY_CANDIDATES),\
              $(shell $(p) -c "from PIL import features; raise SystemExit(not features.check('raqm'))" 2>/dev/null && echo $(p)))
PY_PIL  = $(foreach p,$(PY_CANDIDATES),\
              $(shell $(p) -c 'import PIL' 2>/dev/null && echo $(p)))
PYIMG ?= $(firstword $(PY_RAQM) $(PY_PIL))
# The system art compiler needs Pillow 9.1+, which the Raqm Python may lack
# (pyenv's has 9.0); PYART selects another one.
PY_PIL91 = $(foreach p,$(PY_CANDIDATES),\
              $(shell $(p) -c 'import sys, PIL; sys.exit(tuple(map(int, PIL.__version__.split(".")[:2])) < (9, 1))' 2>/dev/null && echo $(p)))
PYART ?= $(firstword $(PY_PIL91))

# The system art (res/system-art/original, ZyperOS's own) compiled for the
# launcher: res/system-icons and src/systemart_data.h (tools/systemart.py).
# The check holds the compiler, its output against the art, and the art
# against its own records (tools/systemart-original-check.py, its hashes,
# poses and timing; tools/systemart-symbol-pixels-check.py, PICO-8's and
# TIC-80's moving symbols). The compiler requires Pillow 9.1 or newer. PYART
# can select a local venv.
.PHONY: system-art check-systemart
system-art:
	@py='$(PYART)'; [ -n "$$py" ] || { echo "Pillow 9.1+ is required; set PYART" >&2; exit 1; }; \
	"$$py" -c 'import PIL; assert tuple(map(int, PIL.__version__.split(".")[:2])) >= (9,1), "Pillow 9.1+ is required"' && \
	"$$py" -B tools/systemart.py

check-systemart:
	@py='$(PYART)'; [ -n "$$py" ] || { echo "Pillow 9.1+ is required; set PYART" >&2; exit 1; }; \
	"$$py" -c 'import PIL; assert tuple(map(int, PIL.__version__.split(".")[:2])) >= (9,1), "Pillow 9.1+ is required"' && \
	"$$py" -B tools/systemart-check.py && "$$py" -B tools/systemart.py --check && \
	"$$py" -B tools/systemart-original-check.py && "$$py" -B tools/systemart-symbol-pixels-check.py

# What MENU > Controls says: every button on exactly one page, and rows short
# enough to sit still. The page is the device's own copy of the controls list,
# so what it omits is invisible until somebody goes looking for a button.
check-controls: build-native/controls-check
	@./build-native/controls-check

# The remap rules, pure: Diatom's map language read and written, the swap rule
# of a button's change, what is stored for a scope, and the choices a system
# offers (DESIGN.md 11.45). And your PICO-8's table as SDL's mapping line
# (src/p8pad.h).
check-remap: build-native/remap-check
	@./build-native/remap-check

build-native/remap-check: tools/remap-check.c src/remap.c src/remap.h src/remapstore.c src/remapstore.h src/controls.c \
                          src/p8pad.c src/p8pad.h \
                          src/controls.h src/padglyph.h src/menu.h src/config.c src/config.h \
                          src/db.c src/db.h src/atomic.c config/systems.cfg \
                          $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/remap-check.c src/remap.c src/remapstore.c src/controls.c src/hotkeys.c src/powerhold.c src/config.c src/db.c src/atomic.c \
	      src/p8pad.c \
	      $(I18N_SRC) -lsqlite3

# The player's hotkeys, pure: SELECT and a button for each action, the swap
# that loses none, what is kept for Every Game, a system and a game, and
# Diatom's RUN form (src/hotkeys.h, src/hkstore.h)
check-hotkeys: build-native/hotkeys-check
	@./build-native/hotkeys-check

build-native/hotkeys-check: tools/hotkeys-check.c src/hotkeys.c src/hotkeys.h src/hkstore.c src/hkstore.h \
                            src/db.c src/db.h src/atomic.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/hotkeys-check.c src/hotkeys.c src/hkstore.c src/db.c src/atomic.c -lsqlite3

# Rumble's levels, the share a game is sent, and what is kept for a game, a
# system and MENU (src/rumble.h)
check-rumble: build-native/rumble-check
	@./build-native/rumble-check

build-native/rumble-check: tools/rumble-check.c src/rumble.c src/rumble.h src/config.c src/config.h \
                           src/db.c src/db.h src/atomic.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/rumble-check.c src/rumble.c src/config.c src/db.c src/atomic.c \
	      $(I18N_SRC) -lsqlite3

# The equalizer's settings, presets and the file a port's plugin reads
# (src/eq.h), and its arithmetic on test tones, with its cost (src/eqdsp.h)
check-eq: build-native/eq-check
	@./build-native/eq-check

build-native/eq-check: tools/eq-check.c src/eq.c src/eq.h src/eqdsp.h src/db.c src/db.h src/atomic.c \
                       $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/eq-check.c src/eq.c src/db.c src/atomic.c $(I18N_SRC) -lsqlite3 -lm

check-eqdsp: build-native/eqdsp-check
	@./build-native/eqdsp-check

build-native/eqdsp-check: tools/eqdsp-check.c src/eqdsp.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O2 -o $@ tools/eqdsp-check.c -lm

# The ports' equalizer plugin (plastron's zyperos-eq), built for this computer
# and loaded the way alsa-lib's ladspa PCM loads it: flat untouched, Voice
# heard, the launcher's file followed, its cut (your PICO-8 quiet while MP4
# plays) faded to silence and back, and its cost; then, on Linux, a port's
# whole chain (eq-port.sh) opened as SDL opens it at every rate a program
# asks, over a stand-in for the dmix (tools/eqchain-check.sh: PICO-8's
# 22050 Hz was refused on 1.1.0-rc.5, with no sound). tools/eqplugin-alsa.sh
# runs it through the handheld's own alsa-lib, by hand (it needs the SDK).
check-eqplugin: build-native/eqplugin-check build-native/zyperos_eq.so
	@./build-native/eqplugin-check build-native/zyperos_eq.so
	@sh tools/eqchain-check.sh

build-native/zyperos_eq.so: plastron/buildroot/package/zyperos-eq/src/zyperos_eq.c \
                            plastron/buildroot/package/zyperos-eq/src/ladspa.h src/eqdsp.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O2 -fPIC -shared -Isrc -o $@ \
	      plastron/buildroot/package/zyperos-eq/src/zyperos_eq.c -lm

build-native/eqplugin-check: tools/eqplugin-check.c src/eqdsp.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O2 -o $@ tools/eqplugin-check.c -lm -ldl

# The in-game Controls pages and the home's A to Z order, drawn
check-controls-shots:
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@PYIMG='$(PYIMG)' sh tools/controls-shots-check.sh

build-native/controls-check: tools/controls-check.c tools/legend.h src/controls.c \
                            src/controls.h src/remap.c src/remap.h src/padglyph.h src/menu.h src/config.c src/config.h \
                            src/db.c src/db.h src/atomic.c config/systems.cfg \
                            $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/controls-check.c src/controls.c src/hotkeys.c src/powerhold.c src/remap.c src/config.c src/db.c src/atomic.c \
	      $(I18N_SRC) $(DEVICE_PROFILES) -lsqlite3

build-native/menu-check: tools/menu-check.c tools/legend.h src/wifi_menu.c src/wifi.c \
                        src/sys_menu.c src/homesys.c src/homesys.h src/menu.c src/game_menu.c src/controls.c src/remap.c \
                        src/ctltab.c src/ctltab.h src/tdkey.h src/padglyph.h \
                        src/device/td_model.h src/device/td_model.c src/device/pixel2_model.c \
                        src/device/brick_model.c src/audioout.c src/sidelights.c src/wifi_menu.h src/sys_menu.h \
                        src/game_menu.h src/menu.h src/audioout.h src/sidelights.h \
                        src/controls.h src/spawn.c src/spawn.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -o $@ \
	      tools/menu-check.c src/wifi_menu.c src/wifi.c src/sys_menu.c src/homesys.c \
	      src/menu.c src/game_menu.c src/controls.c src/hotkeys.c src/powerhold.c src/remap.c src/ctltab.c src/audioout.c \
	      src/sidelights.c src/keyhints.c src/spawn.c src/device/td_model.c src/device/pixel2_model.c \
	      src/device/brick_model.c $(I18N_SRC)

# What the launcher tells the side lights' gauge (src/sidelights.h): the lines
# as plastron's gauge reads them (its lights.c linked in, for the setting's
# line), the FIFO writer that never waits and never dies of SIGPIPE, on a
# real FIFO, and when a File Transfer batch is over.
GAUGE_SRC := plastron/buildroot/package/gauge/src
check-sidelights: build-native/sidelights-check
	@./build-native/sidelights-check

build-native/sidelights-check: tools/sidelights-check.c src/sidelights.c src/sidelights.h \
                               $(GAUGE_SRC)/lights.c $(GAUGE_SRC)/lights.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/sidelights-check.c src/sidelights.c $(GAUGE_SRC)/lights.c $(I18N_SRC)

build-native/xfer-check: tools/xfer-check.c src/xfer.c src/xfer.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/xfer-check.c src/xfer.c $(I18N_SRC)

# Search Games' matching (src/search.h): what a query finds and in what order.
check-search: build-native/search-check
	@./build-native/search-check

build-native/search-check: tools/search-check.c src/search.c src/search.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/search-check.c src/search.c

# USB File Transfer's SSH: what it counts in dropbear's log (sign-ins, wrong
# PINs, a port it cannot have), with user names that try to say otherwise,
# and the shadow it gives dropbear. Host only; dropbear itself is the device's.
check-sshlink: build-native/sshlink-check
	@./build-native/sshlink-check

build-native/sshlink-check: tools/sshlink-check.c src/sshlink.c src/sshlink.h src/rmtree.c src/rmtree.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/sshlink-check.c src/sshlink.c src/rmtree.c $(I18N_SRC)

# USB charges only until asked (src/usblink.h): a computer is asked about once
# per plug and a charger never, every choice ends with its screen or its cable,
# and Developer Mode keeps the link, on by default only on a development image.
# The port and the link are stubs; the settings database is a real one in /tmp,
# and the development image's marker is this tree's build-native/.
check-usblink: build-native/usblink-check
	@./build-native/usblink-check

build-native/usblink-check: tools/usblink-check.c src/usblink.c src/usblink.h src/usbpeer.h src/db.c src/atomic.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -DDEV_SCRIPT='"$(CURDIR)/build-native/usblink-check-S50dropbear"' \
	      -DUSBLINK_CHECK_SCRIPT='"$(CURDIR)/build-native/usblink-check-S50dropbear"' \
	      -o $@ tools/usblink-check.c src/usblink.c src/db.c src/atomic.c -lsqlite3

# The device profiles (src/device/profile.h): each one whole, today's answers
# for the Pixel 2, the Brick and the host's desktop, and the choice of one by
# its id (--device). Built with all three, as the host build is. No SDL. The
# chip's temperature from the zones a profile names (src/thermal.h), on a
# sysfs made in a scratch folder. Then no device guessed in src/
# (tools/devices-lint.py).
check-devices: build-native/devices-check
	@./build-native/devices-check
	@python3 tools/devices-lint.py

# Each device's build for make and for the image (src/device/build.h,
# tools/devicegen.c): config/device/<id>.mk and <id>.system, kept in git so a
# cross build and plastron read them with no host step. device-files writes
# them; check-devicegen writes them again beside and fails on any that is
# stale, or on a file there that no device writes.
build-native/devicegen: tools/devicegen.c src/device/build.h $(DEVICE_BUILDS) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O1 -g -o $@ tools/devicegen.c $(DEVICE_BUILDS)

.PHONY: device-files
device-files: build-native/devicegen
	@mkdir -p config/device
	./build-native/devicegen config/device

check-devicegen: build-native/devicegen
	@mkdir -p build-native/devicegen-out
	@./build-native/devicegen build-native/devicegen-out
	@bad=0; for f in config/device/*; do \
		n=$${f##*/}; \
		if ! cmp -s "build-native/devicegen-out/$$n" "$$f"; then \
			echo "  FAIL $$f is not what its device's _build.c says (make device-files), or no device's"; bad=1; \
		fi; \
	done; \
	for id in pixel2 brick; do for k in mk system; do \
		[ -f config/device/$$id.$$k ] || { echo "  FAIL no config/device/$$id.$$k (make device-files)"; bad=1; }; \
	done; done; \
	[ $$bad = 0 ] && echo "ok: config/device holds each device's build as its _build.c says"

build-native/devices-check: tools/devices-check.c src/profile.c src/device/profile.h src/device/build.h src/device/brick_buttons.h src/inbutton.h \
                            src/thermal.c src/thermal.h src/ds/button.c src/ds/button.h $(DEVICE_PROFILES) $(DEVICE_BUILDS) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -DZYPEROS_PROFILE=device_profile_desktop -DZYPEROS_PROFILES_ALL \
	      -o $@ tools/devices-check.c src/profile.c src/thermal.c src/ds/button.c $(DEVICE_PROFILES) $(DEVICE_BUILDS)

# Every device file compiled, the Linux halves the Mac's build never sees (CI
# job "devices", .github/workflows/check.yml): the launcher's device files,
# src/profile.c with each device's profile and model, and Diatom's two ports. Syntax
# only, so it needs the headers and no device. Elsewhere it says it skipped,
# so it is not in CHECKS (the macOS job would only ever skip it).
.PHONY: check-device-compile
check-device-compile:
	@if [ "$$(uname -s)" != Linux ]; then echo "skipped: device files compile on Linux (CI job devices)"; exit 0; fi; \
	set -e; F="-std=gnu11 -D_GNU_SOURCE -fsyntax-only -Wall -Wextra -Wno-unused-parameter"; \
	S="$$(pkg-config --cflags sdl2)"; \
	for d in pixel2 brick; do \
		echo "  launcher: src/device/$$d.c and its hardware report"; \
		$(CC) $$F -Isrc $$S src/device/$$d.c $$(ls src/device/$${d}_hw.c 2>/dev/null); \
		echo "  launcher: src/profile.c with src/device/$${d}_profile.c and its model"; \
		$(CC) $$F -Isrc -DZYPEROS_PROFILE=device_profile_$$d src/profile.c src/device/$${d}_profile.c \
			src/device/td_model.c $$(ls src/device/$${d}_model.c 2>/dev/null); \
	done; \
	D="-Idiatom/include -Idiatom/src -Idiatom/vendor/rcheevos/include -Idiatom/vendor/lz4"; \
	echo "  diatom: port/brick.c"; \
	$(CC) $$F $$D $$(pkg-config --cflags sdl2 egl glesv2) \
		diatom/port/brick.c diatom/port/osdbar.c diatom/port/themefile.c diatom/port/hud.c \
		diatom/port/panelgamma.c; \
	echo "  launcher: a test kit's src/device/brick_kit.c and src/device/brick_hw.c (ZYPEROS_KIT_BUILD)"; \
	$(CC) $$F -DZYPEROS_KIT_BUILD -Isrc $$S src/device/brick_kit.c src/device/brick_hw.c; \
	echo "  diatom: port/pixel2.c"; \
	$(CC) $$F $$D $$(pkg-config --cflags sdl2 egl glesv2 gbm libdrm) \
		diatom/port/pixel2.c diatom/port/rumble_evdev.c diatom/port/panelgamma.c; \
	echo "ok: every device file compiles"

# The handhelds' models (src/device/td_model.h): every key once, the GKD
# Pixel 2's places, views and kept boxes as the drawing had them, and the
# TrimUI Brick's as measured, its shoulders on the back ledge. No SDL.
check-model: build-native/model-check
	@./build-native/model-check

build-native/model-check: tools/model-check.c src/device/td_model.h src/device/td_model.c \
                          src/device/pixel2_model.c src/device/brick_model.c src/tdkey.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/model-check.c src/device/td_model.c src/device/pixel2_model.c src/device/brick_model.c

# Developer mode (src/devmode.h): the unlock hold on a clock moved by hand,
# the setting in a real database, the one-time password and the keys the
# player puts on the card.
check-devmode: build-native/devmode-check
	@./build-native/devmode-check

build-native/devmode-check: tools/devmode-check.c src/devmode.c src/devmode.h src/db.c src/atomic.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/devmode-check.c src/devmode.c src/db.c src/atomic.c -lsqlite3

# The launcher's command channel (src/ctl.h), the one px2-open talks to: the
# command lines, a game's path to its system and its shelf, everything not a
# game under Roms turned away, and the socket in a folder in /tmp.
check-ctl: build-native/ctl-check
	@./build-native/ctl-check

build-native/ctl-check: tools/ctl-check.c src/ctl.c src/ctl.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/ctl-check.c src/ctl.c

# Auto Off's clock. It has broken five times, once in this arithmetic and four
# times in wiring; this covers the arithmetic, and `grep -n idle_due src/main.c`
# covers the wiring by listing every screen that honors it.
check-idle: build-native/idle-check
	@./build-native/idle-check

build-native/idle-check: tools/idle-check.c src/idle.c src/idle.h src/chargedark.c \
                         src/chargedark.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/idle-check.c src/idle.c src/chargedark.c

# Long tasks and Auto Off (src/taskhold.h): what a loop says holds the clock,
# lets go by itself, and the real clock does not fire during a task and comes
# back whole after it. Which jobs hold is long_task_busy (src/zyperdraw.c).
check-taskhold: build-native/taskhold-check
	@./build-native/taskhold-check

build-native/taskhold-check: tools/taskhold-check.c src/taskhold.c src/taskhold.h src/idle.c \
                             src/idle.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/taskhold-check.c src/taskhold.c src/idle.c

# The screensaver's policy (src/saver.h): when it starts and what keeps it from
# starting, when it ends on its own, a clip's fades, which one is next, and the
# rule it is built on - Auto Off fires at its own time whatever it does.
check-saver: build-native/saver-check
	@./build-native/saver-check

build-native/saver-check: tools/saver-check.c src/saver.c src/saver.h src/idle.c src/idle.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/saver-check.c src/saver.c src/idle.c

# Date & Time: each row moving only itself, the 12-hour and 24-hour words, the
# shelf's short clock, the zone list running west to east, and Time Zone's
# list with its rule (a zone picked by hand turns Auto off) - see
# tools/clock-check.c.
check-clock: build-native/clock-check
	@./build-native/clock-check

# The launcher's reader of Diatom's answers (src/resident.c), the device
# stubbed, against a made-up Diatom on a socket of its own
# (tools/resident-check.c): SETPLAYER2's answer read while paused, the
# picture on the way noted, a game's EXIT before it left for the wait
# loop rather than eaten, and a running game's volume boost said to MP4 at
# each LEVEL line, none from before its SETLEVEL. SDL for SDL_GetTicks
# alone; skipped without it.
check-resident:
	@if command -v sdl2-config > /dev/null 2>&1; then \
		$(MAKE) --no-print-directory build-native/resident-check && ./build-native/resident-check; \
	else \
		echo "check-resident: skipped, no SDL2"; \
	fi

build-native/resident-check: tools/resident-check.c src/resident.c src/powerhold.c src/platform.h src/device.h FORCE
	@mkdir -p build-native
	@$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g $$(sdl2-config --cflags) \
	      -o $@ tools/resident-check.c src/resident.c src/powerhold.c $$(sdl2-config --libs)

# spawn_detached (src/spawn.h): a program that outlives the launcher, with
# real processes - its own pid back, not this process's child, a failed exec
# a refusal rather than a pid that names nothing, its environment, folder and
# descriptors
check-spawn: build-native/spawn-check
	@./build-native/spawn-check

build-native/spawn-check: tools/spawn-check.c src/spawn.c src/spawn.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O1 -g -o $@ tools/spawn-check.c src/spawn.c

# The launcher's resident.c, the device stubbed, against a real Diatom: the
# zone chosen in Date & Time reaches a game's clock (tools/gamezone-check.c).
# Run by check-diatom, after Diatom's own build.
build-native/gamezone-check: tools/gamezone-check.c src/resident.c src/powerhold.c src/platform.h src/device.h FORCE
	@mkdir -p build-native
	@$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g $$(sdl2-config --cflags) \
	      -o $@ tools/gamezone-check.c src/resident.c src/powerhold.c $$(sdl2-config --libs)

build-native/clock-check: tools/clock-check.c src/clock.c src/clock.h src/menu.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/clock-check.c src/clock.c $(I18N_SRC)

# Date & Time's Auto: ipwho.is's reply read (and refusals, pages that are not
# JSON, replies cut short and names that are paths not), the zone it comes to
# (the name the system has, else the offset's), the row's words, when it
# asks, and 1.0.5's default on scratch databases (on for a new card, switched
# on once for a card from before, a zone picked after it kept) - see
# tools/autozone-check.c.
check-autozone: build-native/autozone-check
	@./build-native/autozone-check

build-native/autozone-check: tools/autozone-check.c src/autozone.c src/autozone.h src/clock.c src/clock.h \
                             src/rajson.c src/rajson.h src/menu.h src/db.c src/db.h src/atomic.c \
                             $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/autozone-check.c src/autozone.c src/clock.c src/rajson.c src/db.c src/atomic.c \
	      $(I18N_SRC) -lsqlite3 -lpthread

# The device's title matcher against the host tool's. A drift here does not
# fail, it just finds fewer games - see tools/artscrape-check.c.
check-artscrape: build-native/artscrape-check
	@ART_SCORING=1 ./build-native/artscrape-check
	@python3 tools/artscrape-check.py

build-native/artscrape-check: tools/artscrape-check.c src/artscrape.c src/artscrape.h src/urlenc.c src/library.c src/sheet.c src/db.c $(I18N_SRC) src/artsrc.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -DZYPEROS_VERSION='"check"' \
	      -DINDEX_TMP='"$(CURDIR)/build-native/artscrape-check-artindex"' -DDAT_TMP='"$(CURDIR)/build-native/artscrape-check-artdat"' \
	      -o $@ tools/artscrape-check.c src/artscrape.c src/artsrc.c src/net.c src/urlenc.c src/library.c src/sheet.c src/db.c $(I18N_SRC) -lsqlite3

# Replace fetches over a cover rather than deleting it first. The real scraper
# against a stubbed network, so a hit and a miss are both pinned with no device
# and no download - see tools/artrun-check.c.
check-artrun: build-native/artrun-check
	@./build-native/artrun-check

build-native/artrun-check: tools/artrun-check.c src/artscrape.c src/artscrape.h src/urlenc.c src/library.c src/sheet.c src/db.c $(I18N_SRC) src/artsrc.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc -DSS_BUSY_WAIT=0 -DSGDB_BUSY_WAIT=0 -DART_QUIET \
	      -DINDEX_TMP='"$(CURDIR)/build-native/artrun-check-artindex"' -DDAT_TMP='"$(CURDIR)/build-native/artrun-check-artdat"' \
	      -o $@ tools/artrun-check.c src/artscrape.c src/artsrc.c src/urlenc.c src/library.c src/sheet.c src/db.c $(I18N_SRC) -lsqlite3

# SteamGridDB on the device (src/sgdb.h): the real client and a whole run
# against a fake API - the key in a header and never a URL, the match, the
# formats, the refusals - and its title and ratio held to the computer tool's
# own Python (tools/sgdb-check.py).
check-sgdb: build-native/sgdb-check
	@./build-native/sgdb-check
	@python3 tools/sgdb-check.py

build-native/sgdb-check: tools/sgdb-check.c src/sgdb.c src/sgdb.h src/sgdbrun.c src/sgdbrun.h src/artscrape.c src/artscrape.h src/rajson.c src/urlenc.c src/library.c src/sheet.c src/db.c $(I18N_SRC) src/artsrc.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -DSGDB_GAP_MS=0 -DSGDB_BUSY_WAIT=0 -DART_QUIET -DSGDB_QUIET \
	      -DSGDB_TMP='"build-native/sgdb-reply.json"' \
	      -DINDEX_TMP='"$(CURDIR)/build-native/sgdb-check-artindex"' -DDAT_TMP='"$(CURDIR)/build-native/sgdb-check-artdat"' \
	      -o $@ tools/sgdb-check.c src/sgdb.c src/sgdbrun.c src/artscrape.c src/artsrc.c src/rajson.c \
	      src/urlenc.c src/library.c src/sheet.c src/db.c $(I18N_SRC) -lsqlite3 -lpthread

# Which systems get videos and for which games (src/snaps.h): off to start,
# 1.0.4's setting kept, a system's own switch, Videos for.
check-snaps: build-native/snaps-check
	@./build-native/snaps-check

build-native/snaps-check: tools/snaps-check.c src/snaps.c src/snaps.h src/db.c src/db.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -o $@ tools/snaps-check.c src/snaps.c src/db.c -lsqlite3

# When the shelf's cover turns into its video snap and back (src/snapshelf.h),
# on a clock the check moves by hand.
check-snapshelf: build-native/snapshelf-check
	@./build-native/snapshelf-check

build-native/snapshelf-check: tools/snapshelf-check.c src/snapshelf.c src/snapshelf.h src/snaps.h src/design.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -o $@ tools/snapshelf-check.c src/snapshelf.c

# The two hashers - one C for the device, one Python for the host tools - over
# every ROM in the library. A wrong rule does not crash; it produces a hash RA
# has never seen, which is indistinguishable from a game RA does not know.
check-rahash: build-native/rahash-check
	@python3 tools/rahash-check.py

# The in-game menu's Achievements tab, as words and rows (src/achtab.h): each
# state its own sentence, in the order that decides it, the counts line, and
# an achievement's row as both lists draw it
check-achtab: build-native/achtab-check
	@./build-native/achtab-check

build-native/achtab-check: tools/achtab-check.c src/achtab.c src/achtab.h src/cheevos.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -o $@ tools/achtab-check.c src/achtab.c $(I18N_SRC)

# The FLAC frame decoder a CHD's FLAC hunks go through (src/flacdec.h), held
# to libFLAC's own encoder at every setting it has. Needs the flac tool
# (Homebrew's flac); skips cleanly without it.
check-flacdec:
	@python3 tools/flacdec-check.py

build-native/rahash-check: tools/rahash-check.c $(RAHASH_SRC) src/rahashjob.c src/rahash.h src/rahashjob.h \
                          src/discimg.h src/chd.h src/flacdec.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g $(LZMA_CFLAGS) \
	      -o $@ tools/rahash-check.c $(RAHASH_SRC) -lz $(LZMA_LIBS) -lpthread

.PHONY: check-noticeq check-badges check-badge-shots check-radl check-dlq check-dl-recent check-dl-look check-cheatsdl check-cheats check-rasubmit check-lowbatt check-battempty check-powerhold check-homesys check-collect check-shotname check-heat check-charging

# The pop-ups over a game, one after another (src/noticeq.h)
check-noticeq: build-native/noticeq-check
	@./build-native/noticeq-check

build-native/noticeq-check: tools/noticeq-check.c src/noticeq.c src/noticeq.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/noticeq-check.c src/noticeq.c

# Achievement badges (src/rabadge.h) and the two pictures the launcher draws
# for them (src/noticeart.h): the cache beside the set, the fetch off the
# frame in one curl for what is missing, the plain badge where the card has
# none, a 404 page never kept as a badge; then the plain badge and Zyper as
# pixels. The real rabadge.c and net.c, net.c read in whole with the check as
# its curl (tools/rabadge-check.c). No network. Needs SDL2 and SDL2_image,
# and skips cleanly without them.
check-badges:
	@if pkg-config --exists sdl2 SDL2_image 2>/dev/null; then \
		mkdir -p build-native; \
		$(CC) -std=gnu11 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -O1 -g \
		  $$(pkg-config --cflags sdl2 SDL2_image) -Isrc \
		  -DNET_TMP='"$(CURDIR)/build-native/badges-check-tmp"' \
		  -DZYPEROS_VERSION='"check"' \
		  -o build-native/rabadge-check tools/rabadge-check.c src/rabadge.c src/noticeart.c \
		  src/zyperplace.c src/blur.c src/cheevos.c src/atomic.c src/db.c \
		  $$(pkg-config --libs sdl2 SDL2_image) -lsqlite3 -lz -lpthread -lm && \
		SDL_VIDEODRIVER=dummy ./build-native/rabadge-check; \
	else echo "skipped: no SDL2 and SDL2_image for the badge check"; fi

# The same, as the launcher draws them (src/main.c, src/notice.c): an unlock's
# notice with a badge or the plain one and Zyper, in both themes; a count's
# with the plain one and no Zyper; the set's arrival with neither; and the
# achievements list with the plain badge in every row, or the card's own.
# The host build's --notice and --cheevos-screen shots, read back with
# Python's standard library (tools/badge-shots-check.py). Needs SDL2, and
# skips cleanly without it.
check-badge-shots:
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@python3 tools/badge-shots-check.py

# RetroAchievements sign-in, telling a wrong password from another refusal
# from no answer: the real rafetch.c and net.c, net.c read in whole with the
# check as its curl, answering as each server would, and the credential rules
# held as it runs: off the command line, in 0600 files made new, all gone
# after (tools/signin-check.c). No network.
check-signin: build-native/signin-check
	@./build-native/signin-check

build-native/signin-check: tools/signin-check.c src/rafetch.c src/rafetch.h src/net.c src/net.h src/rajson.c $(RAHASH_SRC) src/atomic.c src/db.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -DZYPEROS_VERSION='"check"' \
	      -DNET_TMP='"$(CURDIR)/build-native/signin-check-tmp"' \
	      $(LZMA_CFLAGS) -o $@ tools/signin-check.c src/rafetch.c src/rajson.c $(RAHASH_SRC) src/atomic.c src/db.c $(I18N_SRC) -lsqlite3 -lz $(LZMA_LIBS)

# What a refused unlock means for the queue (ra_refusal): settled, the
# account's, or that one row's. No request is made.
check-rasubmit: build-native/rasubmit-check
	@./build-native/rasubmit-check

build-native/rasubmit-check: tools/rasubmit-check.c src/rafetch.c src/rafetch.h src/rajson.c $(RAHASH_SRC) src/net.c src/atomic.c src/db.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -DZYPEROS_VERSION='"check"' \
	      $(LZMA_CFLAGS) -o $@ tools/rasubmit-check.c src/rafetch.c src/rajson.c $(RAHASH_SRC) src/net.c src/atomic.c src/db.c $(I18N_SRC) -lsqlite3 -lz $(LZMA_LIBS)

# Achievements earned offline, sent when a network is back: the store, the
# unlock queue's worker and the request, with this check's own binary for curl
# answering as RetroAchievements does (tools/raoffline-check.c). Makes no
# request. Needs SDL2, for the worker's thread.
check-raoffline:
	@if pkg-config --exists sdl2 2>/dev/null; then \
		mkdir -p build-native; \
		$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
		  $$(pkg-config --cflags sdl2) -Isrc \
		  -DNET_TMP='"$(CURDIR)/build-native/raoffline-check-tmp"' \
		  -DZYPEROS_VERSION='"check"' \
		  -o build-native/raoffline-check tools/raoffline-check.c src/raqueue.c \
		  src/cheevos.c src/rafetch.c src/rajson.c $(RAHASH_SRC) src/atomic.c src/db.c \
		  $(I18N_SRC) $(LZMA_CFLAGS) $$(pkg-config --libs sdl2) -lsqlite3 -lz $(LZMA_LIBS) -lpthread && \
		./build-native/raoffline-check; \
	else echo "skipped: no SDL2 for the offline achievements check"; fi

# MENU > Achievements' Download (src/radl.h): a whole card's run against a
# RetroAchievements of the check's own - this check is its curl, net.c read in
# whole - on a mixed card: sets right and out of date, new, unknown, empty, a
# marker RetroAchievements has news for, a 429, the network gone mid-run, a
# run stopped and finished by the next, libraries that never come, a DS hash
# kept from before the DS hash, an account refused. The time is the check's.
# No network.
check-radl: build-native/radl-check
	@./build-native/radl-check

# The downloads' queue (src/dlq.h) and net.c's slots: two lanes side by side,
# a game, a stop, the card's floor, the step hook and the runs, against this
# check's own curl. The floor is past any disk so the room rule fires on its
# first look.
# A game's cheats with no device: the .cht reader and the list (src/cht.h,
# src/cheats.h) on a card in a folder of its own
check-cheats: build-native/cheats-check
	@./build-native/cheats-check

build-native/cheats-check: tools/cheats-check.c src/cheats.c src/cheats.h src/cht.c src/cht.h src/atomic.c src/atomic.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -DCHEATS_TMP='"$(CURDIR)/build-native/cheats-check-tmp"' \
	      -o $@ tools/cheats-check.c src/cheats.c src/cht.c src/atomic.c

check-dlq: build-native/dlq-check
	@./build-native/dlq-check

build-native/dlq-check: tools/dlq-check.c src/dlq.c src/dlq.h src/net.c src/net.h src/monoclock.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -DZYPEROS_VERSION='"check"' \
	      -DNET_TMP='"$(CURDIR)/build-native/dlq-check-tmp"' \
	      -DDLQ_FLOOR='(1ULL << 62)' -DDLQ_ROOM_MS=1 \
	      -o $@ tools/dlq-check.c

# MENU > Downloads left with B and opened again (src/dl_screen.c), on the real
# launcher with a stand-in run: what came is listed as it was, its pictures
# with it, what came behind the page too, nothing in a game, the wall of a run
# that ended behind it, the pictures read again after the renderer went
check-dl-recent:
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@python3 tools/dl-recent-check.py

# A game's panel in MENU > Downloads looking at the card (src/dl_screen.c's
# one_look), on the real launcher: Videos is On card only for a game whose
# video is there, its own snap or EmulationStation's, and not for one with
# none, an empty snap or a snap deleted after a look
check-dl-look:
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@python3 tools/dl-look-check.py

# The cheats download (src/cheatsdl.h): a zip made in the check, read, matched,
# written and counted, with no network
check-cheatsdl: build-native/cheatsdl-check
	@./build-native/cheatsdl-check

build-native/cheatsdl-check: tools/cheatsdl-check.c src/cheatsdl.c src/cheatsdl.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc $(shell pkg-config --cflags sdl2 2>/dev/null) \
	      -o $@ tools/cheatsdl-check.c -lz

build-native/radl-check: tools/radl-check.c src/radl.c src/radl.h src/net.c src/net.h src/rafetch.c src/rajson.c src/cheevos.c src/library.c src/sheet.c $(RAHASH_SRC) src/atomic.c src/db.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -DZYPEROS_VERSION='"check"' \
	      -DNET_TMP='"$(CURDIR)/build-native/radl-check-tmp"' \
	      -DRADL_TMP='"$(CURDIR)/build-native"' \
	      $(LZMA_CFLAGS) -o $@ tools/radl-check.c src/radl.c src/rafetch.c src/rajson.c src/cheevos.c \
	      src/library.c src/sheet.c $(RAHASH_SRC) src/atomic.c src/db.c $(I18N_SRC) -lsqlite3 -lz $(LZMA_LIBS) -lpthread

# The two set converters - one C on the device, one Python in the bulk tool -
# over real RetroAchievements responses. Needs credentials and a network;
# skips cleanly without them.
check-raset: build-native/raset-check
	@python3 tools/raset-check.py

build-native/raset-check: tools/raset-check.c src/rafetch.c src/rajson.c $(RAHASH_SRC) src/net.c src/atomic.c src/db.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -DZYPEROS_VERSION='"check"' \
	      $(LZMA_CFLAGS) -o $@ tools/raset-check.c src/rafetch.c src/rajson.c $(RAHASH_SRC) src/net.c src/atomic.c src/db.c $(I18N_SRC) -lsqlite3 -lz $(LZMA_LIBS)

# Whether this build can reach ScreenScraper, and whether an account survives
# a restart. The live half needs a network and .screenscraper.env, and skips
# cleanly without them - the same bargain check-raset makes.
check-ss: build-native/ss-check
	@./build-native/ss-check

build-native/ss-check: tools/ss-check.c src/ss.c src/ss.h src/ssfetch.c src/ssfetch.h src/db.c src/net.c src/urlenc.c src/rajson.c src/library.c src/sheet.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	@$(MAKE) -f mk/native.mk VERSION=$(VERSION) creds
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Ibuild-native \
	      -DZYPEROS_VERSION='"check"' \
	      -o $@ tools/ss-check.c src/ss.c src/ssfetch.c src/db.c src/net.c src/urlenc.c src/rajson.c src/library.c src/sheet.c $(I18N_SRC) -lsqlite3 -lpthread

# Scraped display names: SQLite upgrade, real parser/runner, library fallback,
# and the actual main.c refresh hooks with headless view stubs.
check-titles: build-native/title-check build-native/title-refresh-check build-native/title-bulk-check
	@./build-native/title-check
	@./build-native/title-refresh-check
	@./build-native/title-bulk-check

build-native/title-check: tools/title-check.c src/db.c src/library.c src/sheet.c src/ssfetch.c src/ssrun.c src/rajson.c src/urlenc.c $(I18N_SRC) src/artsrc.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -DSS_TMP='"build-native/title-reply.json"' \
	      -o $@ tools/title-check.c src/db.c src/library.c src/sheet.c src/ssfetch.c src/ssrun.c src/artsrc.c src/rajson.c src/urlenc.c $(I18N_SRC) -lsqlite3

build-native/title-bulk-check: tools/title-bulk-check.c src/artscrape.c src/db.c src/library.c src/sheet.c src/ssfetch.c src/ssrun.c src/rajson.c src/urlenc.c $(I18N_SRC) src/artsrc.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc -DSS_BUSY_WAIT=0 -DSGDB_BUSY_WAIT=0 -DART_QUIET \
	      -DSS_TMP='"build-native/title-bulk-reply.json"' \
	      -DINDEX_TMP='"$(CURDIR)/build-native/title-bulk-check-artindex"' -DDAT_TMP='"$(CURDIR)/build-native/title-bulk-check-artdat"' \
	      -o $@ tools/title-bulk-check.c src/artscrape.c src/artsrc.c src/db.c src/library.c src/sheet.c src/ssfetch.c src/ssrun.c src/rajson.c src/urlenc.c $(I18N_SRC) -lsqlite3

build-native/title-refresh-check: tools/title-refresh-check.c tools/title-refresh-hooks.py src/main.c src/scraper_screen.c src/library.c src/sheet.c src/db.c src/stats.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	python3 tools/title-refresh-hooks.py src/main.c src/scraper_screen.c build-native/title-refresh-hooks.h
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Ibuild-native \
	      -o $@ tools/title-refresh-check.c src/library.c src/sheet.c src/db.c src/stats.c $(I18N_SRC) -lsqlite3

# Host build of the launcher, for working on how the shelf looks.
native:
	$(MAKE) $(NATIVE_J) -f mk/native.mk VERSION=$(VERSION)

# The Pixel 2's card payload from this tree, against plastron's SDK (its make
# sdk), in Docker: build/px2/payload/ZyperOS, for tools/px2-deploy.sh or
# tools/qemu-run.sh deploy. tools/px2-payload.sh says how.
px2-payload:
	sh tools/px2-payload.sh

# The programs alone, for a change to the code: build/px2/programs/ZyperOS,
# no cores (tools/px2-payload.sh --programs). tools/px2 deploy sends them.
.PHONY: px2-programs
px2-programs:
	sh tools/px2-payload.sh --programs

# The libretro cores ZyperOS redistributes, fetched from its own cores
# release and hash-pinned (mk/fetch-vendor.sh). Needed once, before the first
# payload. No runtime libraries: the cores need only what the device's
# firmware already has.
vendor:
	./mk/fetch-vendor.sh

# mk/fetch-vendor.sh's pins, offline: the cores release and nothing else, a
# tag no installer takes for a version, and each core's source named
# (tools/corepins-check.py).
check-corepins:
	@python3 tools/corepins-check.py

# Assets in res/ are committed ready to ship; nothing needs generating to build.
# Four have make rules, run by hand when what they are drawn from changes: the
# boot animation (`make boot`), the system sprites (`make system-art`), the
# interface sounds (`make sounds`, above) and the design tokens (`make design`,
# which writes DESIGN.md's tables, src/design.h, the mock kit's tokens.js and
# the token block in res/web/app.css). Two have generators of their own, run as
# they say: tools/gensingles.py writes res/singles.png, and tools/genzyper.py
# the stills of Zyper and the ZyperOS lettering (res/readme/, res/web/ and
# src/zypermark.h). PortMaster's covers and icon (res/portmaster/) are copies
# of the system art's (res/system-art/original/utility), which check-pmart holds.
boot:
	python3 tools/genboot.py

# Two of those generators' art against what they draw now: tools/genloading.py's
# loading loop and loading.txt in res/loading/, and tools/genfirststart.py's
# first start's line and bar in res/boot/first-start/. Drawn again into a temp
# folder and compared, a PNG by its pixels (tools/gendrift.py). Both draw the
# same bytes run after run; the first start's line is text, and these pictures
# are what a Pillow with Raqm draws, the one PYIMG picks first. Not in CHECKS.
.PHONY: check-genloading check-genfirststart check-genhudglyphs check-genclosed
# The FPS readout's glyphs (src/hudglyphs.h and Diatom's copy) as
# tools/genhudglyphs.py draws them, with the Pillow they were drawn with
check-genhudglyphs:
	@py='$(PYIMG)'; [ -n "$$py" ] || { echo "check-genhudglyphs: needs Pillow; set PYIMG" >&2; exit 1; }; \
	"$$py" -B tools/genhudglyphs.py --check

check-genloading:
	@py='$(PYIMG)'; [ -n "$$py" ] || { echo "check-genloading: needs Pillow; set PYIMG" >&2; exit 1; }; \
	"$$py" -B tools/genloading.py --check

check-genfirststart:
	@py='$(PYIMG)'; [ -n "$$py" ] || { echo "check-genfirststart: needs Pillow; set PYIMG" >&2; exit 1; }; \
	"$$py" -B tools/genfirststart.py --check

# The TrimUI Brick's screen for a card without its closed files, plastron's
# splash's closed-missing*.bin, as tools/genclosed.py draws it
check-genclosed:
	@py='$(PYIMG)'; [ -n "$$py" ] || { echo "check-genclosed: needs Pillow; set PYIMG" >&2; exit 1; }; \
	"$$py" -B tools/genclosed.py --check

# Fourteen config files became two databases, so properties that used to be
# obvious from looking at a text file now need asserting - chiefly that a
# shipped default never overwrites a choice, which is a bug this project
# already shipped once.
check-db: build-native/db-check
	@./build-native/db-check

# A game's core options (src/coreopts.h): the player's rows, then the
# device's defaults, then every device's, in the library's order; and the
# one-time cleanup of what 1.0.5 seeded, which keeps the player's own.
check-coreopts: build-native/coreopts-check
	@./build-native/coreopts-check

build-native/coreopts-check: tools/coreopts-check.c src/coreopts.c src/coreopts.h src/db.c src/db.h \
                             src/atomic.c src/strength.c src/device/pixel2_coreopts.c src/device/profile.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -o $@ tools/coreopts-check.c src/coreopts.c src/db.c src/atomic.c src/strength.c \
	      src/device/pixel2_coreopts.c -lsqlite3 -lpthread

# MP4's settings under its own name (src/mp4keys.h): a handheld's "muse."
# settings from before 1.1.0 moved under "mp4." at its first start, each
# value kept; the new one kept where both are; nothing for a second start
check-mp4keys: build-native/mp4keys-check
	@./build-native/mp4keys-check

build-native/mp4keys-check: tools/mp4keys-check.c src/mp4keys.c src/mp4keys.h src/db.c src/db.h \
                            src/atomic.c src/strength.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -o $@ tools/mp4keys-check.c src/mp4keys.c src/db.c src/atomic.c src/strength.c \
	      -lsqlite3 -lpthread

# The saved library a start shows before it reads the card (src/savedlib.h):
# a listing written, read and finished is lib_scan's, the check's digest sees
# changes and not order, and nothing but a whole file is read as one; then
# four starts of the real launcher, headless, with a game added before the
# third (tools/savedlib-e2e.sh).
check-savedlib: build-native/savedlib-check
	@./build-native/savedlib-check
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@sh tools/savedlib-e2e.sh

build-native/savedlib-check: tools/savedlib-check.c src/savedlib.c src/savedlib.h src/library.c src/sheet.c src/library.h \
                             src/db.c src/db.h src/atomic.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/savedlib-check.c src/savedlib.c src/library.c src/sheet.c src/db.c src/atomic.c -lsqlite3 -lpthread

build-native/db-check: tools/db-check.c src/db.c src/db.h src/atomic.c src/strength.c src/strength.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/db-check.c src/db.c src/atomic.c src/strength.c -lsqlite3 -lpthread

# Delete Game (src/gamedel.h) on a card made of small files: a cartridge, a
# multi-disc set in its folder, a .cue with its tracks and an arcade set
# beside neogeo.zip, each with saves, states, a cover, a video and a set.
# Exactly the game's files go, never a neighbour's, never neogeo.zip, never
# a file another game shares; its rows in the library database go and the
# neighbours' stay; a sheet or a folder that leads out of the card's folders
# is refused with nothing touched.
#
# Then the real launcher, headless (tools/gamedel-e2e.py): Delete Game's yes
# through --delete-now, the shelf after it in place and on the next game or
# the one before, from Favorites, down to the empty home, and the saved
# library a start reads first.
check-gamedel: build-native/gamedel-check
	@./build-native/gamedel-check

# Rename (src/gamern.h): a game and everything named after it, on a card
# of small files, what others share copied, the library rows following
check-gamern: build-native/gamern-check
	@./build-native/gamern-check

build-native/gamern-check: tools/gamern-check.c src/gamern.c src/gamern.h src/gamedel.c src/sheet.c src/gamedel.h \
                           src/esmedia.c src/esmedia.h src/library.c src/db.c src/favorites.c src/stats.c \
                           src/portinfo.c src/atomic.c src/snaps.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -o $@ tools/gamern-check.c src/gamern.c src/gamedel.c src/sheet.c src/esmedia.c src/library.c src/db.c \
	      src/favorites.c src/stats.c src/portinfo.c src/atomic.c src/snaps.c $(I18N_SRC) -lsqlite3 -lpthread

# EmulationStation's folders and media (src/esmedia.h): covers, videos,
# details and the import on a card of small files in three ES layouts
check-esmedia: build-native/esmedia-check
	@./build-native/esmedia-check

build-native/esmedia-check: tools/esmedia-check.c src/esmedia.c src/esmedia.h src/library.c src/sheet.c src/library.h \
                            src/db.c src/db.h src/snaps.c src/snaps.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -o $@ tools/esmedia-check.c src/esmedia.c src/library.c src/sheet.c src/db.c src/snaps.c -lsqlite3 -lpthread
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@python3 tools/gamedel-e2e.py

build-native/gamedel-check: tools/gamedel-check.c src/gamedel.c src/sheet.c src/gamedel.h src/portinfo.c src/portinfo.h src/library.c \
                            src/library.h src/db.c src/db.h src/favorites.c src/stats.c \
                            src/atomic.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/gamedel-check.c src/gamedel.c src/sheet.c src/portinfo.c src/library.c src/db.c src/favorites.c \
	      src/stats.c src/atomic.c $(I18N_SRC) -lsqlite3 -lpthread

# A game's Cover and Video page (DESIGN.md 11.34), what a device cannot show
# you: where a cover came from as its record's line (src/artsrc.h); Use as
# Cover and Put Back on a card of small files, the cover there was to
# .media/.before and the records with it, both ways, and a copy that fails
# changing nothing; an MP4's length from its mvhd (src/mediafile.c).
# The MP4 parser reads files a server sent: the check runs under
# AddressSanitizer and UBSan where the compiler has them, so a read out of
# bounds fails it rather than passing by luck
MEDIA_SAN := $(shell echo 'int main(void){return 0;}' | $(CC) -fsanitize=address,undefined -x c - -o /dev/null 2>/dev/null && echo -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)

check-media: build-native/media-check
	@./build-native/media-check

build-native/media-check: tools/media-check.c src/mediafile.c src/mediafetch.h src/artsrc.c src/artsrc.h \
                          src/db.c src/db.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g $(MEDIA_SAN) \
	      -o $@ tools/media-check.c src/mediafile.c src/artsrc.c src/db.c -lsqlite3

# MENU > Storage's numbers (src/storage.h) on a card made of small files:
# every part, every game's bytes and the bar held to the clusters the card's
# own files take; the cache read on a second start with no folder listed
# again, a game added listing its own folder alone, a cache it cannot read
# measured again; the two recommendations deleting exactly their files; the
# media pages' albums, films and books coming to their folders and deleting
# only themselves; and X's orders. Then the real launcher, headless: Delete
# Game's question opening on the page from nothing, as the page is a base
# (tools/storage-motion-check.py), and every list in each of X's orders with
# the media pages, in both themes (tools/storage-pages-check.py).
check-storage: build-native/storage-check
	@./build-native/storage-check
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@python3 tools/storage-motion-check.py
	@python3 tools/storage-pages-check.py

build-native/storage-check: tools/storage-check.c src/storage.c src/storage.h src/gamedel.c src/sheet.c src/portinfo.c \
                            src/gamedel.h src/library.c src/db.c src/favorites.c src/stats.c \
                            src/atomic.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/storage-check.c src/storage.c src/gamedel.c src/sheet.c src/portinfo.c src/library.c src/db.c \
	      src/favorites.c src/stats.c src/atomic.c $(I18N_SRC) -lsqlite3 -lpthread

# Play time. The assertion that earns this its place is that a LAUNCH writes
# nothing: that is a claim about a path nobody watches, and it stops being true
# one reasonable-looking write at a time.
check-stats: build-native/stats-check
	@./build-native/stats-check

build-native/stats-check: tools/stats-check.c src/stats.c src/stats.h src/db.c src/atomic.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/stats-check.c src/stats.c src/db.c src/atomic.c $(I18N_SRC) -lsqlite3

# What order a shelf ends up in. Two of the four orders read the session rows,
# so this links stats and the database - but not library.c: game_entry is a
# struct in the header and nothing here scans a directory.
check-sort: build-native/sort-check
	@./build-native/sort-check

build-native/sort-check: tools/sort-check.c src/sort.h src/library.h src/stats.c \
                         src/stats.h src/db.c src/atomic.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/sort-check.c src/stats.c src/db.c src/atomic.c $(I18N_SRC) -lsqlite3

# Bluetooth. A device NAME is arbitrary bytes chosen by whoever owns the
# headset, arriving over the air into a process running as root; an ADDRESS is
# the one value handed back to bluetoothctl. This checks the boundary between
# them.
check-bt: build-native/bt-check
	@./build-native/bt-check

build-native/bt-check: tools/bt-check.c tools/legend.h src/bt.c src/bt.h src/bt_menu.c \
                      src/bt_menu.h src/spawn.c src/spawn.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/bt-check.c src/bt.c src/bt_menu.c src/keyhints.c src/spawn.c $(I18N_SRC)

# MP4's view of the Music folder: the two shapes it reads, the litter a Mac
# leaves beside every track, and where an album's cover is kept.
check-mp4lib: build-native/mp4lib-check
	@./build-native/mp4lib-check

build-native/mp4lib-check: tools/mp4lib-check.c src/mp4lib.c src/mp4lib.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/mp4lib-check.c src/mp4lib.c $(I18N_SRC)

# The logs pack: names and Bluetooth addresses masked, and a whole pack
# unpacked with the same tar and read back.
check-logpack: build-native/logpack-check
	@./build-native/logpack-check

build-native/logpack-check: tools/logpack-check.c src/logpack.c src/logpack.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/logpack-check.c src/logpack.c

# The play modes: the last track, the first, a shuffle running out, a track
# that will not open, and a mode changed under a playing track.
# MP4's lock screen: every button blocked, power and volume too, and L1 + R1
# held two seconds the one way out.
check-mp4lock: build-native/mp4lock-check
	@./build-native/mp4lock-check

# MP4's Player Looks' motion (src/mp4look.h): bars up at once and down on
# follow's tau at any frame rate, caps held for peak-hold and down with
# gravity, every bar at rest after, the bands over a look's bars.
check-mp4look: build-native/mp4look-check
	@./build-native/mp4look-check

# START on MP4's shelf turns its View (DESIGN.md 11.67), on the real launcher
# in desk mode's dummy window, as check-desk runs it: Covers, Grid, List and
# round (no Video), kept as view.MUSE, and SELECT with START, the lock's
# pair, in either order, turns nothing.
check-mp4views: native
	@python3 tools/mp4views-check.py

# MP4 back after a power-off (src/mp4resume.h, DESIGN.md 11.67): the row
# written and read back, a broken one refused, a session's writes counted;
# then the real launcher started on cards with each kind of row, in desk
# mode's dummy window: Now Playing paused at its second, only for a row that
# says playing, and nothing for one whose file is gone. And a start back into
# a game, against a Diatom of the check's own: the game first, MP4 held under
# it, and SELECT in the game's menu Now Playing on it, paused. Each wait is
# for the screen or the row it names, never for a time.
check-mp4resume: build-native/mp4resume-check native
	@./build-native/mp4resume-check
	@python3 tools/mp4resume-start.py

build-native/mp4resume-check: tools/mp4resume-check.c src/mp4resume.c src/mp4resume.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -o $@ tools/mp4resume-check.c src/mp4resume.c -lm

build-native/mp4look-check: tools/mp4look-check.c src/mp4look.c src/mp4look.h src/design.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/mp4look-check.c src/mp4look.c -lm

# An album cover's main color (src/covercolor.h), the cassette's stripe and
# the vinyl's: one color, a title on white, the larger of two, grey.
check-covercolor: build-native/covercolor-check
	@./build-native/covercolor-check

build-native/covercolor-check: tools/covercolor-check.c src/covercolor.c src/covercolor.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/covercolor-check.c src/covercolor.c -lm

# The daemon's bands for the bars (src/mp4d/bands.h): each band lit by a tone
# at its own middle, silence dark, the window where the listener is and not
# where the decoder got to, and its time. `build-native/bands-check --song
# FILE` plays a song through it, which is how its scale was set.
check-bands: build-native/bands-check
	@./build-native/bands-check

build-native/bands-check: tools/bands-check.c src/mp4d/bands.c src/mp4d/bands.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O2 -g \
	      -o $@ tools/bands-check.c src/mp4d/bands.c -lm -lpthread

build-native/mp4lock-check: tools/mp4lock-check.c src/mp4lock.c src/mp4lock.h src/inbutton.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/mp4lock-check.c src/mp4lock.c

# MP4's video frames between the daemon and the launcher (src/mp4frame.h): a
# writer and a reader on two threads over the same pages, every picture the
# reader keeps whole.
check-p2ws: build-native/p2ws-check
	@./build-native/p2ws-check

build-native/p2ws-check: tools/p2ws-check.c src/p2d/ws.c src/p2d/ws.h src/p2d/sha1.c src/p2d/sha1.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/p2ws-check.c src/p2d/ws.c src/p2d/sha1.c

check-mp4frame: build-native/mp4frame-check
	@./build-native/mp4frame-check

build-native/mp4frame-check: tools/mp4frame-check.c src/mp4frame.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O2 -g \
	      -o $@ tools/mp4frame-check.c -lpthread

# MP4's video picture of any shape - 4:3 (1440x1080, 480x360), 16:9, odd,
# tiny, the largest, the size changing inside one file - published as
# src/mp4d/vid.c publishes it and read by src/mp4video.c into its texture: as
# wide as the frame's rows, a multiple of 128 (src/mp4frame.h, mpf_stride: the
# Pixel 2's Mesa ran past a texture's buffer for other widths), each plane
# fitting Mesa's tile buffer as rows, drawn whole from its own rectangle,
# nothing out of bounds (UBSan where the compiler has it, and Guard Malloc on
# a Mac). Needs SDL2.
check-mp4size:
	@if pkg-config --exists sdl2 2>/dev/null; then \
		$(MAKE) --no-print-directory build-native/mp4size-check || exit 1; \
		if [ -f /usr/lib/libgmalloc.dylib ]; then gm=/usr/lib/libgmalloc.dylib; else gm=; fi; \
		SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software DYLD_INSERT_LIBRARIES=$$gm \
			./build-native/mp4size-check; \
	else echo "check-mp4size: skipped, no SDL2"; fi

build-native/mp4size-check: tools/mp4size-check.c src/mp4video.c src/mp4video.h src/mp4frame.h FORCE
	@mkdir -p build-native
	san=; echo 'int main(void){return 0;}' | $(CC) -fsanitize=undefined -x c - -o /dev/null 2>/dev/null && \
		san='-fsanitize=undefined -fno-sanitize-recover=all'; \
	$(CC) -std=gnu11 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -O1 -g $$san -Isrc \
	      -DMP4SIZE_FRAMES='"$(CURDIR)/build-native/mp4size-frames"' $$(pkg-config --cflags sdl2) \
	      -o $@ tools/mp4size-check.c $$(pkg-config --libs sdl2) -lm

# Player 2's frame codec (src/p2d/codec.c): a frame of palette indices, XORed
# with the last one, raw-deflated; and the decoder the page mirrors. The check
# round-trips 100 frames, writes the fixtures the page's test reads, and prints
# the encode time, so it builds with -O2 to mean something. check-play runs it
# too, so it is built by its sources rather than FORCE, and in check-prep.
check-p2codec: build-native/p2codec-check
	@./build-native/p2codec-check

build-native/p2codec-check: tools/p2codec-check.c src/p2d/codec.c src/p2d/codec.h
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O2 -g \
	      -o $@ tools/p2codec-check.c src/p2d/codec.c -lz

# Player 2's shared file (src/p2share.h): Diatom's frames out, p2d's pads in,
# run against each other on one block of memory; and the header's offsets and
# SETPLAYER2's rule for a system's buttons and players.
check-p2share: build-native/p2share-check
	@./build-native/p2share-check

build-native/p2share-check: tools/p2share-check.c src/p2share.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O2 -g \
	      -o $@ tools/p2share-check.c

# The handheld's theme as the play page's theme.css (src/webtheme.c): with
# nothing customised the file gives the very values of play.css's own token
# blocks, for the properties play.css lets it set and no others; a custom
# look moves the accent, Square makes the corners 0, dark says so, a buffer
# too small is refused and the write leaves a whole file. check-play serves
# a file this binary makes (--emit), so it is built by its sources rather
# than FORCE, and in check-prep.
check-webtheme: build-native/webtheme-check
	@./build-native/webtheme-check

build-native/webtheme-check: tools/webtheme-check.c src/webtheme.c src/webtheme.h src/look.c \
                             src/look.h src/theme.h src/design.h res/web/play/play.css
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O2 -g \
	      -o $@ tools/webtheme-check.c src/webtheme.c src/look.c -lm

# p2d, NES player 2's server (src/p2d/main.c), on the host: the play page,
# the WebSocket of frames and buttons, the hotspot's DNS. The launcher's own
# httpd and captive.c, linked as mk/cross.mk links them for the device, and
# -O2 as there: it encodes 60 frames a second. Built by its sources rather
# than FORCE, as is p2d-feed below: check-p2d and check-play both run them,
# and two makes writing one binary at once under make check's parallel run
# was a race. check-prep builds them first, so neither check builds them then.
P2D_SRC = $(wildcard src/p2d/*.c) src/httpd.c src/captive.c
build-native/p2d: $(P2D_SRC) $(wildcard src/p2d/*.h) src/httpd.h src/captive.h src/p2share.h \
                  src/monoclock.h src/xfer.h
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -O2 -g \
	      -o $@ $(P2D_SRC) -lz

# Frames into a share file as Diatom writes them (tools/p2d-feed.c): a moving
# picture, or a fixture's frames in order, for p2d's check and the play
# page's.
build-native/p2d-feed: tools/p2d-feed.c src/p2share.h
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O2 -g \
	      -o $@ tools/p2d-feed.c

# p2d against a feed, over real sockets (tools/p2d-check.py): the page and
# the captive redirect, frames only as fast as they are acked, the pad into
# the share and let go when the guest goes quiet or leaves, a guest past the
# system's players turned away and told so, two players at once with their
# own pads and frames, a KEY on the page's 'K', the end of the game however
# it comes and "stopped" for a SIGTERM with the game going on, the page never
# framed, --addr alone listened on, few wakeups with no guest, and the DNS
# answer.
check-p2d: build-native/p2d build-native/p2d-feed
	@python3 tools/p2d-check.py

# The play page (res/web/play), player 2's picture and pad, in headless Chrome
# over build-native/p2d and a feed of the codec check's fixture frames
# (tools/play-check.py): the canvas the frame it says it drew, every key and
# touch to the share's pad, the pad drawn from the game's buttons and nothing
# sent outside them, the picture in whole device pixels, Picture off and the
# KEY asked for after it, a page left with Back freeing its place, a second
# browser told the game is full and trying again, a browser too old for the
# page, multiplayer turned off and on again, and the game's end. The fixtures go
# into the check's own temp folder, never build-native's, which check-p2codec
# may be writing at the same moment. Chrome is needed.
check-play: build-native/p2d build-native/p2d-feed build-native/p2codec-check build-native/webtheme-check
	@python3 tools/play-check.py

# The launcher's side of Player 2 (src/p2.c), on fakes for every process and
# protocol action: the system table (the NES's core alone), the link it
# picks, the page's address, Diatom's answer read by its pairs, the order it
# starts in on the hotspot (p2d only once the hotspot is up) and on the
# cable, a refusal, the stop in the reverse order after any end of a game
# (SIGKILL a second after a SIGTERM p2d ignores, and a second stop doing
# nothing), a notice as each guest comes and goes, a failure the tick meets
# without waiting, and a start after a stop or a failure that reads Diatom's
# old answer away and takes its own.
check-p2: build-native/p2-check
	@./build-native/p2-check

build-native/p2-check: tools/p2-check.c src/p2.c src/p2.h src/hotspot.h src/p2share.h \
                       src/webtheme.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/p2-check.c src/p2.c

check-mp4queue: build-native/mp4queue-check
	@./build-native/mp4queue-check

build-native/mp4queue-check: tools/mp4queue-check.c src/mp4queue.c src/mp4queue.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/mp4queue-check.c src/mp4queue.c

# Album covers from MusicBrainz: the rule that picks the record, against what
# MusicBrainz said about the first card's albums, and a run on stubbed network.
check-mp4art: build-native/mp4art-check
	@./build-native/mp4art-check

build-native/mp4art-check: tools/mp4art-check.c src/mp4art.c src/mp4art.h \
                            src/urlenc.c src/rajson.c tools/fixtures/musicbrainz/*.json $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/mp4art-check.c src/mp4art.c src/urlenc.c src/rajson.c $(I18N_SRC)

# src/zypermark.h is Zyper as triangles, generated from tools/zyperdef.py
# because C cannot import it; this proves it is current, and that the ground
# he is drawn on is Salt & Pepper's in tools/designdef.py. Part of `make
# check`, for the same reason payload.sh refuses a card whose systems.cfg names
# cores vendor/ does not have: the failure is silent otherwise, and it ships.
checkmark:
	python3 tools/checkmark.py

clean:
	rm -rf build build-native out

# The Shader list's pictures (src/shaderdemo.h): tools/shaderdemo.c makes
# res/shaderdemo/ from Diatom's own filters in C, every filter and Strength
# step for each panel. After make shaderdemo, look at them, then record what
# they were made from (tools/shaderdemo-check.py --write). check-shaderdemo
# fails where they are not what the tool makes, where an offered filter has
# none, and where Diatom's filters changed since they were made.
build-native/shaderdemo: tools/shaderdemo.c
	@mkdir -p build-native
	$(CC) -std=gnu11 -O2 -Wall -Wextra $$(pkg-config --cflags sdl2 SDL2_image) \
	  -o $@ tools/shaderdemo.c $$(pkg-config --libs sdl2 SDL2_image) -lm

.PHONY: shaderdemo check-shaderdemo
shaderdemo: build-native/shaderdemo
	./build-native/shaderdemo res/shaderdemo

check-shaderdemo: build-native/shaderdemo
	@'$(PYIMG)' tools/shaderdemo-check.py

# ZyperOS's own screens' shader (src/screenfx.h): its picture held to
# Diatom's GLSL through tools/shaderdemo.c's port of it, at a game's size,
# every pixel, what the Pixel 2 shows of each old screen, and the masks
# kept and used again (src/screenfx_draw.c) on SDL's software renderer.
build-native/screenfx-check: tools/screenfx-check.c tools/shaderdemo.c src/screenfx.c src/screenfx.h \
                             src/screenfx_draw.c src/screenfx_draw.h
	@mkdir -p build-native
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Isrc $$(pkg-config --cflags sdl2 SDL2_image) \
	  -o $@ tools/screenfx-check.c src/screenfx.c src/screenfx_draw.c $$(pkg-config --libs sdl2 SDL2_image) -lm

.PHONY: check-screenfx
check-screenfx: build-native/screenfx-check
	@./build-native/screenfx-check

# The graded bar (src/ds/scale.h): a scale value reads back as written and
# keeps its range, words are never one, and the knob is drawn where the value
# is, zero's mark through the track, in both themes; the bars' columns and
# the colour chart's hue band, copied from their textures (ui_bake), are the
# pixels they drew, and a frame of Screen Colors' bars and chart keeps to a
# budget of drawing calls.
check-scale:
	@if pkg-config --exists sdl2 SDL2_ttf SDL2_image; then \
		mkdir -p build-native; \
		$(CC) -std=gnu11 -D_GNU_SOURCE -O1 -g -Wall -Wextra \
		  $$(pkg-config --cflags sdl2 SDL2_ttf SDL2_image) -Isrc \
		  -o build-native/scale-check tools/scale-check.c src/ds/scale.c src/ds/colorref.c src/ui.c src/ds/glyph.c src/ds/button.c src/folderart.c src/roundrect.c src/theme.c src/look.c \
		  src/themefile.c src/linebreak.c src/blur.c src/cardgen.c src/cardart.c src/cardart_draw.c src/sysmark.c $(I18N_SRC) \
		  $$(pkg-config --libs sdl2 SDL2_ttf SDL2_image) -lm && \
		ZYPEROS_HEADLESS=1 SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
		  ./build-native/scale-check; \
	else echo "skipped: no SDL2 for the scale check"; fi

# Repeated frames through the real text cache: a line first drawn during a
# fade must reach the same full-opacity pixels as an uncached opaque line.
check-textfade:
	@if pkg-config --exists sdl2 SDL2_ttf SDL2_image; then \
		mkdir -p build-native; \
		$(CC) -std=gnu11 -D_GNU_SOURCE -O1 -g -Wall -Wextra \
		  $$(pkg-config --cflags sdl2 SDL2_ttf SDL2_image) -Isrc \
		  -o build-native/textfade-check tools/textfade-check.c src/ui.c src/ds/glyph.c src/ds/button.c src/folderart.c src/roundrect.c src/theme.c src/look.c \
		  src/themefile.c src/linebreak.c src/blur.c src/cardgen.c src/cardart.c src/cardart_draw.c src/sysmark.c $(I18N_SRC) \
		  $$(pkg-config --libs sdl2 SDL2_ttf SDL2_image) -lm && \
		ZYPEROS_HEADLESS=1 SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
		  ./build-native/textfade-check; \
	else echo "skipped: no SDL2 for the text fade check"; fi

# The design system's rules the compiler cannot hold (DESIGN.md section 15):
# src/ds/ stands alone, and no page foot, page Zyper or tile mark is typed
# again or read off a name outside it.
.PHONY: check-ds
check-ds:
	@python3 tools/ds-check.py

# The layout ratchet (round 4): each source's typed layout numbers (a layout
# #define set to a number, words at a typed height, a rectangle typed in
# place) held to tools/layout-ratchet.txt, which is only ever lowered: a
# screen moved onto the layout engine or the tokens takes its places by rule
.PHONY: check-layout-ratchet
check-layout-ratchet:
	@python3 tools/layout-ratchet.py

# The layout engine's rules (src/ds/flex.c), a thousand random trees that
# stay in their parents the same way every time, and its time per screen
.PHONY: check-flex
check-flex: build-native/flex-check
	@./build-native/flex-check

build-native/flex-check: tools/flex-check.c src/ds/flex.c src/ds/flex.h
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O2 -Isrc $$(pkg-config --cflags sdl2) -o $@ tools/flex-check.c src/ds/flex.c

# What an option list's cursor shows (src/ds/hover.c): a show once per choice
# the cursor comes to, the kept one back with undo, and an undo on close only
# for what was shown
.PHONY: check-hover
check-hover: build-native/hover-check
	@./build-native/hover-check

build-native/hover-check: tools/hover-check.c src/ds/hover.c src/ds/hover.h
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O2 -Isrc -o $@ tools/hover-check.c src/ds/hover.c

# Theme's three lists, whose cursors show the choice, swept: every choice
# shown and the kept one back with no database row and no file written, the
# page reading the same after, and one show's cost (tools/hover-sweep-check.py)
.PHONY: check-hover-sweep
check-hover-sweep:
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@python3 tools/hover-sweep-check.py

# The marks drawn from strokes (src/ds/glyph.c): every one within its strokes
# and arrowheads, none dropped, and the cache giving the same pixels when full.
.PHONY: check-glyphs
check-glyphs:
	@if pkg-config --exists sdl2; then \
		mkdir -p build-native; \
		$(CC) -std=gnu11 -D_GNU_SOURCE -O1 -g -Wall -Wextra $$(pkg-config --cflags sdl2) -Isrc \
		  -o build-native/glyph-check tools/glyph-check.c src/ds/glyph.c $$(pkg-config --libs sdl2) -lm && \
		SDL_VIDEODRIVER=dummy ./build-native/glyph-check; \
	else echo "skipped: no SDL2 for the glyph check"; fi

# Tutorial drawing across frames: low-alpha first frames and a theme round-trip
# must not poison cached text, details or bars. No window or device required.
check-tour-colors:
	@if pkg-config --exists sdl2 SDL2_ttf SDL2_image; then \
		mkdir -p build-native; \
		$(CC) -std=gnu11 -D_GNU_SOURCE -O1 -g -Wall -Wextra \
		  $$(pkg-config --cflags sdl2 SDL2_ttf SDL2_image) -Isrc \
		  -o build-native/tour-colors-check tools/tour-colors-check.c src/ui.c src/ds/glyph.c src/ds/button.c src/folderart.c src/roundrect.c \
		  src/theme.c src/themefile.c src/look.c src/linebreak.c src/blur.c src/cardgen.c src/cardart.c src/cardart_draw.c \
		  src/sysmark.c src/tour.c src/powerhold.c src/tourdraw.c src/tourdevice.c src/zyperrig.c src/osdbar.c src/db.c $(I18N_SRC) $(DEVICE_PROFILES) \
		  $$(pkg-config --libs sdl2 SDL2_ttf SDL2_image) -lsqlite3 -lm && \
		ZYPEROS_HEADLESS=1 SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
		  ./build-native/tour-colors-check; \
	else echo "skipped: no SDL2 for the tutorial color check"; fi

# About's card storage and controller capacity estimates, including missing
# sysfs files, malformed values and block-count overflow. No SDL or device.
check-about: build-native/about-check
	@./build-native/about-check

build-native/about-check: tools/about-check.c src/about.c src/about.h src/menu.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -o $@ tools/about-check.c src/about.c $(I18N_SRC)

# About page cues stay inside the panel instead of floating over the shelf.
check-about-shots:
	@if pkg-config --exists sdl2 SDL2_ttf SDL2_image; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@python3 tools/about-shot-check.py

# Random Game's pick (src/randgame.h): every game as likely as every other,
# nothing off a shelf, never the last pick again.
check-randgame: build-native/randgame-check
	@./build-native/randgame-check

build-native/randgame-check: tools/randgame-check.c src/randgame.c src/randgame.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/randgame-check.c src/randgame.c

# Folders on a shelf (src/shelfdir.h): what a folder shows, its folders first
# by name with their games counted at every depth, then its games; a disc set
# kept in a folder a game there; a folder's own folder.
check-shelfdir: build-native/shelfdir-check
	@./build-native/shelfdir-check

build-native/shelfdir-check: tools/shelfdir-check.c src/shelfdir.c src/shelfdir.h src/library.c src/sheet.c \
                             src/library.h src/db.c src/atomic.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/shelfdir-check.c src/shelfdir.c src/library.c src/sheet.c src/db.c src/atomic.c \
	      -lsqlite3 -lpthread
.PHONY: check-home
check-home:
	@$(MAKE) --no-print-directory $(NATIVE_J) -f mk/native.mk VERSION=$(VERSION) build-native/home-check
	@SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software ./build-native/home-check

.PHONY: check-home-shots
check-home-shots: native
	@'$(PYIMG)' tools/home-shot-check.py

# The line under a game's name on Favorites: its own system (src/games.c)
.PHONY: check-favorites-shots
# Your PICO-8 on the host (tools/pico8-shots.py): the PICO-8 shelf with
# Splore before and after the player's PICO-8 is on the card, the Core row and
# the page that says how to add it, in both themes
.PHONY: pico8-shots
pico8-shots: native
	@'$(PYIMG)' tools/pico8-shots.py

check-favorites-shots: native
	@'$(PYIMG)' tools/favorites-shot-check.py

# A system turned off the home keeps its games on Favorites and Recents, and
# they start from there on its own core (src/shelfbuild.c, app.parked)
.PHONY: check-homesys-groups
check-homesys-groups: native
	@'$(PYIMG)' tools/homesys-groups-check.py

# A late frame left undrawn reaches Diatom when the launcher starts a Diatom
# of its own for the game (no resident one), as RUN's frameskip=late does
.PHONY: check-lateskip-launch
check-lateskip-launch: native
	@python3 tools/lateskip-launch-check.py

# The song's name in the top bar slides when too long, and Now Playing's foot
# is a row of hints (src/home.c, src/main.c)
.PHONY: check-np-footer
check-np-footer: native
	@'$(PYIMG)' tools/np-footer-check.py

# Whether a video is too big for the CPU, which MP4 then says (src/mp4d/vidtry.h):
# the pictures shown and dropped the Pixel 2 measured with dav1d (720p60 and
# 1080p30 AV1 never, 1080p60 within 6 s), taken back when it turns light, a
# seek's catch-up not counted
.PHONY: check-vidtry
check-vidtry: build-native/vidtry-check
	@./build-native/vidtry-check

build-native/vidtry-check: tools/vidtry-check.c src/mp4d/vidtry.h src/mp4frame.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -o $@ tools/vidtry-check.c

# Every option row keeps one rule (DESIGN.md 9, src/menu.h's menu_choice):
# more than two choices open their list on A, two turn over, and every table
# says what its row says, on the launcher's own pages (--rows-report)
.PHONY: check-menu-rows
check-menu-rows: native
	@'$(PYIMG)' tools/menu-rows-check.py

# The art badge (src/artbadge.c): every badge in both themes at every size a
# row draws one - placed and sized with its card, its glyph 3:1 on its disc
# and inside it, the shadow below - and on the real launcher's shelves: a
# heart on a favorite's cover and none on another's, the three kinds on MP4's
# All and none on one kind alone (tools/artbadge-shot-check.py)
check-artbadge: build-native/artbadge-check
	@./build-native/artbadge-check

build-native/artbadge-check: tools/artbadge-check.c src/artbadge.c src/artbadge.h \
                            src/design.h src/clamp.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/artbadge-check.c src/artbadge.c src/look.c -lm

.PHONY: check-artbadge-shots
check-artbadge-shots: native
	@'$(PYIMG)' tools/artbadge-shot-check.py

# The play time under a game's name sits where Favorites' system line does, and
# the status corner over a paused game is the home header (src/games.c, main.c)
.PHONY: check-layout-shots
check-layout-shots: native
	@'$(PYIMG)' tools/layout-shot-check.py

# The card of a game with no art (src/cardart.h) on the launcher's own shots:
# a card for each system, the frame of play once played (the card frame over
# the Auto picture), the same card in both themes, one reading line on the
# Grid. Builds the host launcher; needs Pillow.
check-cardart-shots: native
	@'$(PYIMG)' tools/cardart-shot-check.py

# The tutorial's Theme and Home Screen previews, res/tour/home-LAYOUT-THEME.png
# (DESIGN.md 11.10, "The choices"): the launcher's own home screen of a card
# holding the little shelf's five systems. tour-previews makes them again;
# check-tour-previews fails when the home screen has changed and they were
# not made again. Builds the host launcher; needs Pillow.
.PHONY: tour-previews check-tour-previews
tour-previews: native
	@'$(PYIMG)' tools/tour-previews.py

check-tour-previews: native
	@'$(PYIMG)' tools/tour-previews.py --check

.PHONY: check-games
# A video snap of any shape - 640x480, a Nintendo DS game's 320x480, a Game
# Boy game's, 534x480, a width of 1, odd ones - published as MP4 publishes
# it, read into its texture and drawn on the shelf and on a game's Cover and
# Video page: its own shape where it is drawn, a texture never under 640x480
# (src/mp4frame.h, mpf_tex_size: the Pixel 2's GPU faulted on narrower ones),
# nothing out of bounds (UBSan, and Guard Malloc on a Mac). Needs SDL2.
check-snapsize:
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory $(NATIVE_J) -f mk/native.mk VERSION=$(VERSION) build-native/snapsize-check || exit 1; \
		if [ -f /usr/lib/libgmalloc.dylib ]; then gm=/usr/lib/libgmalloc.dylib; else gm=; fi; \
		SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software DYLD_INSERT_LIBRARIES=$$gm \
			./build-native/snapsize-check > build-native/snapsize-check.log 2>&1; st=$$?; \
		grep -v '^fit: ' build-native/snapsize-check.log; exit $$st; \
	else echo "check-snapsize: skipped, no SDL2"; fi

# Every texture the launcher draws into (SDL_TEXTUREACCESS_TARGET) made
# PLAT_TARGET_FORMAT, and that a format SDL's GLES2 renderer keeps unswapped:
# in an ARGB8888 one, a video snap on the Pixel 2 came out with red and blue
# swapped (src/platform.h). A reading of the source; no SDL.
check-targets:
	@python3 tools/targets-check.py

# The same with the device's own SDL and its GLES2 renderer, in Docker
# (tools/yuvtarget-check.sh): video bars through a PLAT_TARGET_FORMAT target
# come out their own colors. Not in CHECKS: Docker, the network, minutes.
yuvtarget:
	sh tools/yuvtarget-check.sh

check-games:
	@$(MAKE) --no-print-directory $(NATIVE_J) -f mk/native.mk VERSION=$(VERSION) build-native/games-check
	@SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software ./build-native/games-check

# The Shader lists' pictures load as textures with alpha, which the Pixel 2's
# SDL needs to draw them into the screen opaque
check-shaderpic:
	@$(MAKE) --no-print-directory $(NATIVE_J) -f mk/native.mk VERSION=$(VERSION) build-native/shaderpic-check
	@SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software ./build-native/shaderpic-check

# The card for a game or an album with no art, drawn on an art worker rather
# than on the frame: the frame's pixels byte for byte, every kind of title,
# both themes, a game's and an album's; a card of the theme that was never
# handed to the shelf after a turn; the fonts closed and opened again around
# a fallback game while cards are asked for; two cards drawn at once by the
# two workers, as two covers are decoded. On the launcher's own objects.
.PHONY: check-cardpaint
check-cardpaint:
	@$(MAKE) --no-print-directory $(NATIVE_J) -f mk/native.mk VERSION=$(VERSION) build-native/cardpaint-check
	@SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software ./build-native/cardpaint-check

# Finished pictures put on screen a few a frame (src/putpace.h): the rules
# with the costs handed in, then the workers' own pictures on a software
# renderer, slow uploads included. On the launcher's own objects.
.PHONY: check-putpace
check-putpace:
	@$(MAKE) --no-print-directory $(NATIVE_J) -f mk/native.mk VERSION=$(VERSION) build-native/putpace-check
	@SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software ./build-native/putpace-check

# The frame thread's time while a shelf lands, for games with no art and with
# box art (tools/landbench.c). A measurement, not in CHECKS: it prints times.
.PHONY: landbench
landbench:
	@$(MAKE) --no-print-directory $(NATIVE_J) -f mk/native.mk VERSION=$(VERSION) build-native/landbench
	@SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software ./build-native/landbench

# tools/from-rogue.sh, the move off a ROGUE OS card: its table against
# systems.cfg (every shelf it fills lists the types it brings, its track
# files are ones the shelf does not list, every disc BIOS a shelf checks for
# comes over, a pending system is not a shelf yet) and against the folders on
# My card (tools/fixtures/rogue-card.txt), and the tool itself on a
# ROGUE card made up of small files: what goes where, the ROGUE side the
# same after, nothing overwritten, nothing half-copied, a second run a no-op.
# POSIX sh, awk and find; offline.
.PHONY: check-from-rogue
check-from-rogue:
	@sh tools/from-rogue-check.sh
