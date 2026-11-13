# ZyperOS. The device builds, for the TrimUI Brick and the GKD Pixel 2, are
# plastron's (plastron/ in this repository): its Buildroot builds the
# launcher and Muse with mk/cross.mk and lays out the card with mk/payload.sh,
# and the version below is read from here. What this Makefile does is the rest:
# the offline checks, a host build of the launcher, the cores, and the assets.
# docs/building.md says how to work on a device.

.PHONY: all clean native px2-payload vendor boot system-art checkmark \
        check check-cheevos check-hare check-portal check-httpd check-idle check-taskhold check-saver check-rahash check-signin check-search \
        check-raset check-xfer check-sshlink check-usblink check-devmode check-ctl check-menus check-status check-battery check-corner check-artscrape check-artrun check-sgdb check-snapshelf check-audioout \
        check-db check-gamedel check-gamern check-esmedia check-corepins check-targets yuvtarget check-storage check-stats check-sort check-bt check-ss check-titles check-dsv check-sidelights check-osdbar check-themefile check-pkkeys check-portkeys-flow check-portkeys check-zyhome check-theme check-config check-kbnav check-tween check-menumotion check-menusnap check-cardgen check-blur check-roundrect check-rowmacro check-emptyhome check-zyperrig check-randgame check-shelfdir \
        check-sysmark check-flip check-menushots check-controls-shots check-design check-systemart check-linebreak check-portload \
        check-zyper check-uisound check-bgm check-bgm-tracks check-sounds check-messages check-tour check-tour-shots check-textfade check-tour-colors check-about check-about-shots check-update check-sysupdate check-pminstall check-pmhelper check-portdoc check-boottheme check-px2-push check-captive check-mdns check-linksteps check-usbrole check-usbdrive check-usbpower check-usblog check-clients check-keyhints check-pace check-qr \
        check-home check-home-shots check-games check-cardpaint check-putpace landbench check-favorites-shots check-np-footer check-layout-shots check-artbadge check-artbadge-shots design sounds strings cjk-font check-strings check-i18n-shots \
        check-tour-previews tour-previews check-prep check-diatom check-simulator check-px2sim check-building-doc check-blog \
        check-installer release

all: check

# The Mac's (or Linux's) cores. mk/native.mk builds one object a source file,
# so its calls below ask for -j: unless this make already has -j, whose jobs
# they share then (make 3.81 puts -jN, or a j among the one-letter flags, in
# MAKEFLAGS).
NPROC := $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
NATIVE_J = $(if $(filter -j%,$(MAKEFLAGS))$(findstring j,$(filter-out -%,$(firstword $(MAKEFLAGS)))),,-j$(NPROC))

# Everything offline, in one command. There was no umbrella target: every check
# had to be remembered by name, which is a suite in the same sense that a list
# of good intentions is a plan. A check nobody runs is a check that does not
# exist, and check-menus was about to join eight others in that state.
CHECKS = check-cheevos check-hare check-portal check-httpd check-idle check-taskhold check-saver check-rahash check-rasubmit check-raoffline check-signin check-noticeq check-badges check-badge-shots \
         check-raset check-xfer check-sshlink check-usblink check-devmode check-ctl check-menus check-artscrape check-artrun check-sgdb check-snapshelf check-audioout \
         check-db check-gamedel check-gamern check-esmedia check-media check-snapsize check-corepins check-targets check-storage check-stats check-sort check-bt check-ss check-titles check-savedlib check-search \
         check-muselib check-musequeue check-museart check-mp4lock check-mp4frame check-p2share check-webtheme check-p2ws check-p2codec check-p2d check-play check-p2 check-resident check-controls check-controls-shots check-logpack \
         check-clock check-status check-battery check-battery-eta check-dslang check-dsresume check-corestate check-core-stamp check-px2-perf check-px2-snap check-px2 check-lowbatt check-heat check-charging check-corner check-dsv check-sidelights check-osdbar check-themefile check-pkkeys check-portkeys-flow check-portkeys check-zyhome check-theme check-config check-kbnav check-randgame check-shelfdir check-look \
         check-tween check-menumotion check-menusnap check-cardgen check-blur check-roundrect check-rowmacro check-emptyhome check-zyperrig check-sysmark check-flip check-menushots check-design \
         check-systemart check-linebreak check-portload check-zyper check-uisound check-bgm check-bgm-tracks check-sounds check-messages \
         check-tour check-tour-shots check-tour-previews check-textfade check-tour-colors check-about check-about-shots check-update check-sysupdate check-pminstall check-pmhelper check-pmart check-portdoc check-boottheme check-px2-push check-captive check-mdns check-linksteps check-usbrole check-usbdrive check-usbpower check-usblog check-clients check-keyhints check-pace check-qr \
         check-home check-home-shots check-games check-cardpaint check-putpace check-favorites-shots check-np-footer check-layout-shots check-artbadge check-artbadge-shots checkmark \
         check-installer check-from-rogue check-importers check-diatom check-simulator check-px2sim check-building-doc check-blog \
         check-strings check-i18n-shots

# All of them at once, as many at a time as there are cores (CHECK_JOBS=1 for
# one after another), each into build-native/check-logs/: tools/check-run.py
# prints a line as each ends and the whole log of any that failed. One after
# another the suite took 167 s on an M3 (2026-11-06), with fifteen cores idle.
.PHONY: $(CHECKS)
check: check-prep
	@python3 tools/check-run.py $(CHECKS)

# What more than one check builds, built once before any of them starts: the
# host launcher, which nine checks draw with, and the ScreenScraper header it
# shares with check-ss (without SDL2, the header alone); and player 2's p2d,
# its feed, the codec check and the theme writer's check, which check-p2d,
# check-p2codec, check-webtheme and check-play run.
check-prep:
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

# docs/building.md's table names every check in CHECKS and nothing else:
# the table had 32 of 91 rows (2026-11-06) and said nothing of the rest.
check-building-doc:
	@python3 tools/building-doc-check.py

# The simulator's tests (simulator/Tests: its VNC client, the px2sim link, the
# state lines and the control socket's commands), with Swift's own build kept
# in build-native/. Skipped where there is no Swift.
check-simulator:
	@if command -v swift > /dev/null 2>&1; then \
		cd simulator && swift test --scratch-path ../build-native/simulator; \
	else \
		echo "check-simulator: skipped, no Swift"; \
	fi

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
VERSION ?= 1.0.5

# A release, from this Mac (tools/release.sh): the Pixel 2's card image built
# clean by plastron from this commit; PUBLISH=1 then makes the draft GitHub
# release and pushes the tag, and GitHub adds ZyperOS Installer for macOS,
# Windows and Linux and publishes it (.github/workflows/release.yml).
release:
	@bash tools/release.sh $(if $(PUBLISH),--publish)

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

build-native/hare-check: tools/hare-check.c src/hare.c src/clients.c src/httpd.c src/xfer.c src/muselib.c src/captive.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -O1 -g \
	      -DHARE_PORTS=0 -DPICK_TMP='"$(CURDIR)/build-native/hare-check-albumpick"' \
	      -DART_NAMES_TMP='"$(CURDIR)/build-native/hare-check-artnames"' -DREPO_WEB='"$(CURDIR)/res/web"' \
	      -o $@ tools/hare-check.c src/hare.c src/clients.c src/httpd.c src/xfer.c src/muselib.c src/captive.c $(I18N_SRC) -lz

# The File Transfer page on the host, over a scratch card: the same routes and
# server, for a browser and for tools/readme-shots.py's pictures of the page.
# The page itself, in headless Chrome over that server: its folders' art, the
# hidden dot folders, the room, the time left, the handheld's stop, the
# update over the cable and its name after the link it came over
# (tools/portal-check.py). Chrome is needed.
check-portal: build-native/portal-serve
	@python3 tools/portal-check.py

build-native/portal-serve: tools/portal-serve.c src/hare.c src/clients.c src/httpd.c src/xfer.c src/muselib.c src/captive.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -O1 -g \
	      -DHARE_PORTS=0 -DPICK_TMP='"$(CURDIR)/build-native/portal-serve-albumpick"' \
	      -DART_NAMES_TMP='"$(CURDIR)/build-native/portal-serve-artnames"' \
	      -o $@ tools/portal-serve.c src/hare.c src/clients.c src/httpd.c src/xfer.c src/muselib.c src/captive.c $(I18N_SRC) -lz

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
# Plain C only: this does not prove the kernel's hardware propagation time.
check-battery: build-native/battery-check
	@./build-native/battery-check

build-native/battery-check: tools/battery-check.c src/battery_poll.c src/battery_poll.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O1 -g \
	      -o $@ tools/battery-check.c src/battery_poll.c

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

# The Nintendo DS's language (src/dslang.h): every language MENU > Language has
# maps to the DS's own or to English, the core's option and its values are the
# patch's (mk/patches/dsperate-lr-language.patch), and the launcher sends it
# first with every DS game and with no other. Plain C for the table; the
# launcher itself, built for the host, for what it sends.
check-dslang: build-native/dslang-check
	@./build-native/dslang-check
	@sh tools/dslang-check.sh

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
# saved 6 and 6 save 5 and 7 through the launcher's src/db.c. No SDL, no device.
check-pkkeys: build-native/pkkeys-check
	@./build-native/pkkeys-check

build-native/pkkeys-check: tools/pkkeys-check.c tools/pkkeys.c tools/pkkeys.h src/keyrepeat.h \
                           src/device/pixel2_levels.h src/audioout.c src/audioout.h \
                           src/db.c src/db.h src/atomic.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -o $@ tools/pkkeys-check.c tools/pkkeys.c src/audioout.c src/db.c src/atomic.c $(I18N_SRC) -lsqlite3

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

# What the Install PortMaster page says in each state (src/pminstall.c,
# DESIGN.md 11.32), and the Ports shelf's PortMaster entry
check-pminstall: build-native/pminstall-check
	@./build-native/pminstall-check

build-native/pminstall-check: tools/pminstall-check.c src/pminstall.c src/pminstall.h src/sysupdate.c src/sysupdate.h src/library.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O1 -g -o $@ tools/pminstall-check.c src/pminstall.c src/sysupdate.c $(I18N_SRC)

# PortMaster's cover in each theme and its page's icon (res/portmaster/*.png)
# against what tools/genportmaster.py draws, pixel for pixel. Needs Pillow.
.PHONY: check-pmart
check-pmart:
	@py='$(PYIMG)'; if [ -z "$$py" ]; then echo "check-pmart: skipped, no Pillow"; \
	else "$$py" -B tools/genportmaster.py --check; fi

# The PortMaster installer itself (res/portmaster/pm-install.py), on this
# computer against a made-up release served from a folder: an install, a
# second press, a checksum that does not match, a zip with a member outside
# PortMaster/, a release list that makes no sense, runtimes kept
check-pmhelper:
	@python3 -I tools/pminstall-run.py

# What a port's own files say for its Controls pages over it (src/portdoc.c:
# port.json, its gptokeyb keymap, its README's controls), and the pages they
# make (src/controls.c), on the made-up ports in tools/fixtures/ports and on
# the forms porters write by hand; PortMaster's own page. No SDL.
check-portdoc: build-native/portdoc-check
	@./build-native/portdoc-check

build-native/portdoc-check: tools/portdoc-check.c src/portdoc.c src/portdoc.h src/rajson.c src/rajson.h \
                           src/controls.c src/controls.h src/portmenu.h src/menu.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -o $@ tools/portdoc-check.c src/portdoc.c \
	      src/rajson.c src/controls.c $(I18N_SRC)

# plastron's make push writes the system slot the Pixel 2 is not running,
# never the one it runs from (scripts/px2-slot.sh): the slot choice, the
# bootstate.txt edits, and the device's script parsing. Host shell only.
check-px2-push:
	@sh plastron/scripts/px2-slot-check.sh

# The boot screen's theme words (src/boottheme.h, DESIGN.md 11.21)
check-boottheme: build-native/boottheme-check
	@./build-native/boottheme-check

build-native/boottheme-check: tools/boottheme-check.c src/boottheme.c src/boottheme.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -O1 -g -o $@ tools/boottheme-check.c src/boottheme.c

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
# system has. No SDL; one scratch file under a directory of its own.
check-config: build-native/config-check
	@./build-native/config-check

build-native/config-check: tools/config-check.c src/config.c src/config.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/config-check.c src/config.c

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
# panel on screen leaving once, and MENU in Muse's menu bringing back what is
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
	      -o $@ tools/tour-check.c src/tour.c src/zyperrig.c src/config.c src/db.c \
	      src/atomic.c $(I18N_SRC) -lsqlite3 -lm

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
# which writes src/i18n_ids.h, src/i18n_tables.c and the File Transfer page's
# res/web/i18n/. The check holds them fresh, every string in all five with
# English's printf conversions, every ID defined where it is used and used
# where it is defined, and every character in the font that draws it.
strings:
	python3 tools/i18n.py

# What a check that links a part of src/ which says T() links beside it: the
# strings and their tables (src/i18n.h). English, as the launcher is before
# the setting is read, unless the check sets another.
I18N_SRC := src/i18n.c src/i18n_tables.c

check-strings:
	@python3 tools/i18n.py --check

# The launcher's main screens in each language and the pseudo-locale, from the
# host build: fails where a string from res/strings is cut, broken past its
# lines or drawn off the screen in any of the five (src/ui.c says so on
# stderr), and lists what the pseudo-locale's longer words do not fit.
check-i18n-shots:
	@python3 tools/i18n-shots.py

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

# Menu music's rules (src/bgm.h): the tracks, the settings, the shuffle, the
# history L2 goes back along, the gain, the Menu Music screen's rows, a
# track's name from its tags (src/bgm_tags.h) and Zyper's pop-up that says
# it (src/songpop.h), and MP4's track in it when L2/R2 move MP4. No SDL and
# no ALSA.
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

# The compiler requires Pillow 9.1 or newer. PYART can select a local venv.
.PHONY: system-art check-systemart
system-art:
	@py='$(PYART)'; [ -n "$$py" ] || { echo "Pillow 9.1+ is required; set PYART" >&2; exit 1; }; \
	"$$py" -c 'import PIL; assert tuple(map(int, PIL.__version__.split(".")[:2])) >= (9,1), "Pillow 9.1+ is required"' && \
	"$$py" -B tools/systemart-draw.py && "$$py" -B tools/systemart.py

check-systemart:
	@py='$(PYART)'; [ -n "$$py" ] || { echo "Pillow 9.1+ is required; set PYART" >&2; exit 1; }; \
	"$$py" -c 'import PIL; assert tuple(map(int, PIL.__version__.split(".")[:2])) >= (9,1), "Pillow 9.1+ is required"' && \
	"$$py" -B tools/systemart-check.py && "$$py" -B tools/systemart-draw.py --check && \
	"$$py" -B tools/systemart.py --check

# What MENU > Controls says: every button on exactly one page, and rows short
# enough to sit still. The page is the device's own copy of the controls list,
# so what it omits is invisible until somebody goes looking for a button.
check-controls: build-native/controls-check
	@./build-native/controls-check

# The in-game Controls pages and the home's A to Z order, drawn
check-controls-shots:
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@sh tools/controls-shots-check.sh

build-native/controls-check: tools/controls-check.c tools/legend.h src/controls.c \
                            src/controls.h src/padglyph.h src/menu.h src/config.c src/config.h \
                            src/db.c src/db.h src/atomic.c config/systems.cfg \
                            $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/controls-check.c src/controls.c src/config.c src/db.c src/atomic.c \
	      $(I18N_SRC) -lsqlite3

build-native/menu-check: tools/menu-check.c tools/legend.h src/wifi_menu.c src/wifi.c \
                        src/sys_menu.c src/menu.c src/game_menu.c src/controls.c \
                        src/audioout.c src/sidelights.c src/wifi_menu.h src/sys_menu.h \
                        src/game_menu.h src/menu.h src/audioout.h src/sidelights.h \
                        src/controls.h src/spawn.c src/spawn.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -o $@ \
	      tools/menu-check.c src/wifi_menu.c src/wifi.c src/sys_menu.c \
	      src/menu.c src/game_menu.c src/controls.c src/audioout.c src/sidelights.c \
	      src/keyhints.c src/spawn.c $(I18N_SRC)

# What the launcher tells the side lights' gauge (src/sidelights.h): the lines
# as plastron's gauge reads them, the FIFO writer that never waits and never
# dies of SIGPIPE, on a real FIFO, and when a File Transfer batch is over.
check-sidelights: build-native/sidelights-check
	@./build-native/sidelights-check

build-native/sidelights-check: tools/sidelights-check.c src/sidelights.c src/sidelights.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/sidelights-check.c src/sidelights.c $(I18N_SRC)

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

build-native/sshlink-check: tools/sshlink-check.c src/sshlink.c src/sshlink.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/sshlink-check.c src/sshlink.c $(I18N_SRC)

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
# shelf's short clock, and the zone list running west to east - see
# tools/clock-check.c.
check-clock: build-native/clock-check
	@./build-native/clock-check

# The launcher's reader of Diatom's answers (src/resident.c), the device
# stubbed, against a made-up Diatom on a socket of its own
# (tools/resident-check.c): SETPLAYER2's answer read while paused, the
# picture on the way noted, and a game's EXIT before it left for the wait
# loop rather than eaten. SDL for SDL_GetTicks alone; skipped without it.
check-resident:
	@if command -v sdl2-config > /dev/null 2>&1; then \
		$(MAKE) --no-print-directory build-native/resident-check && ./build-native/resident-check; \
	else \
		echo "check-resident: skipped, no SDL2"; \
	fi

build-native/resident-check: tools/resident-check.c src/resident.c src/platform.h src/device.h FORCE
	@mkdir -p build-native
	@$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g $$(sdl2-config --cflags) \
	      -o $@ tools/resident-check.c src/resident.c $$(sdl2-config --libs)

# The launcher's resident.c, the device stubbed, against a real Diatom: the
# zone chosen in Date & Time reaches a game's clock (tools/gamezone-check.c).
# Run by check-diatom, after Diatom's own build.
build-native/gamezone-check: tools/gamezone-check.c src/resident.c src/platform.h src/device.h FORCE
	@mkdir -p build-native
	@$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g $$(sdl2-config --cflags) \
	      -o $@ tools/gamezone-check.c src/resident.c $$(sdl2-config --libs)

build-native/clock-check: tools/clock-check.c src/clock.c src/clock.h src/menu.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/clock-check.c src/clock.c $(I18N_SRC)

# The device's title matcher against the host tool's. A drift here does not
# fail, it just finds fewer games - see tools/artscrape-check.c.
check-artscrape: build-native/artscrape-check
	@ART_SCORING=1 ./build-native/artscrape-check
	@python3 tools/artscrape-check.py

build-native/artscrape-check: tools/artscrape-check.c src/artscrape.c src/artscrape.h src/urlenc.c src/library.c src/db.c $(I18N_SRC) src/artsrc.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -DZYPEROS_VERSION='"check"' \
	      -DINDEX_TMP='"$(CURDIR)/build-native/artscrape-check-artindex"' -DDAT_TMP='"$(CURDIR)/build-native/artscrape-check-artdat"' \
	      -o $@ tools/artscrape-check.c src/artscrape.c src/artsrc.c src/net.c src/urlenc.c src/library.c src/db.c $(I18N_SRC) -lsqlite3

# Replace fetches over a cover rather than deleting it first. The real scraper
# against a stubbed network, so a hit and a miss are both pinned with no device
# and no download - see tools/artrun-check.c.
check-artrun: build-native/artrun-check
	@./build-native/artrun-check

build-native/artrun-check: tools/artrun-check.c src/artscrape.c src/artscrape.h src/urlenc.c src/library.c src/db.c $(I18N_SRC) src/artsrc.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc -DSS_BUSY_WAIT=0 -DSGDB_BUSY_WAIT=0 -DART_QUIET \
	      -DINDEX_TMP='"$(CURDIR)/build-native/artrun-check-artindex"' -DDAT_TMP='"$(CURDIR)/build-native/artrun-check-artdat"' \
	      -o $@ tools/artrun-check.c src/artscrape.c src/artsrc.c src/urlenc.c src/library.c src/db.c $(I18N_SRC) -lsqlite3

# SteamGridDB on the device (src/sgdb.h): the real client and a whole run
# against a fake API - the key in a header and never a URL, the match, the
# formats, the refusals - and its title and ratio held to the computer tool's
# own Python (tools/sgdb-check.py).
check-sgdb: build-native/sgdb-check
	@./build-native/sgdb-check
	@python3 tools/sgdb-check.py

build-native/sgdb-check: tools/sgdb-check.c src/sgdb.c src/sgdb.h src/sgdbrun.c src/sgdbrun.h src/artscrape.c src/artscrape.h src/rajson.c src/urlenc.c src/library.c src/db.c $(I18N_SRC) src/artsrc.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -DSGDB_GAP_MS=0 -DSGDB_BUSY_WAIT=0 -DART_QUIET -DSGDB_QUIET \
	      -DSGDB_TMP='"build-native/sgdb-reply.json"' \
	      -DINDEX_TMP='"$(CURDIR)/build-native/sgdb-check-artindex"' -DDAT_TMP='"$(CURDIR)/build-native/sgdb-check-artdat"' \
	      -o $@ tools/sgdb-check.c src/sgdb.c src/sgdbrun.c src/artscrape.c src/artsrc.c src/rajson.c \
	      src/urlenc.c src/library.c src/db.c $(I18N_SRC) -lsqlite3 -lpthread

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

build-native/rahash-check: tools/rahash-check.c src/rahash.c src/rahash.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/rahash-check.c src/rahash.c -lz

.PHONY: check-noticeq check-badges check-badge-shots check-rasubmit check-lowbatt check-heat check-charging

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

build-native/signin-check: tools/signin-check.c src/rafetch.c src/rafetch.h src/net.c src/net.h src/rajson.c src/rahash.c src/atomic.c src/db.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -DZYPEROS_VERSION='"check"' \
	      -DNET_TMP='"$(CURDIR)/build-native/signin-check-tmp"' \
	      -o $@ tools/signin-check.c src/rafetch.c src/rajson.c src/rahash.c src/atomic.c src/db.c $(I18N_SRC) -lsqlite3 -lz

# What a refused unlock means for the queue (ra_refusal): settled, the
# account's, or that one row's. No request is made.
check-rasubmit: build-native/rasubmit-check
	@./build-native/rasubmit-check

build-native/rasubmit-check: tools/rasubmit-check.c src/rafetch.c src/rafetch.h src/rajson.c src/rahash.c src/net.c src/atomic.c src/db.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -DZYPEROS_VERSION='"check"' \
	      -o $@ tools/rasubmit-check.c src/rafetch.c src/rajson.c src/rahash.c src/net.c src/atomic.c src/db.c $(I18N_SRC) -lsqlite3 -lz

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
		  src/cheevos.c src/rafetch.c src/rajson.c src/rahash.c src/atomic.c src/db.c \
		  $(I18N_SRC) $$(pkg-config --libs sdl2) -lsqlite3 -lz -lpthread && \
		./build-native/raoffline-check; \
	else echo "skipped: no SDL2 for the offline achievements check"; fi

# The two set converters - one C on the device, one Python in the bulk tool -
# over real RetroAchievements responses. Needs credentials and a network;
# skips cleanly without them.
check-raset: build-native/raset-check
	@python3 tools/raset-check.py

build-native/raset-check: tools/raset-check.c src/rafetch.c src/rajson.c src/rahash.c src/net.c src/atomic.c src/db.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -DZYPEROS_VERSION='"check"' \
	      -o $@ tools/raset-check.c src/rafetch.c src/rajson.c src/rahash.c src/net.c src/atomic.c src/db.c $(I18N_SRC) -lsqlite3 -lz

# Whether this build can reach ScreenScraper, and whether an account survives
# a restart. The live half needs a network and .screenscraper.env, and skips
# cleanly without them - the same bargain check-raset makes.
check-ss: build-native/ss-check
	@./build-native/ss-check

build-native/ss-check: tools/ss-check.c src/ss.c src/ss.h src/ssfetch.c src/ssfetch.h src/db.c src/net.c src/urlenc.c src/rajson.c src/library.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	@$(MAKE) -f mk/native.mk VERSION=$(VERSION) creds
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Ibuild-native \
	      -DZYPEROS_VERSION='"check"' \
	      -o $@ tools/ss-check.c src/ss.c src/ssfetch.c src/db.c src/net.c src/urlenc.c src/rajson.c src/library.c $(I18N_SRC) -lsqlite3 -lpthread

# Scraped display names: SQLite upgrade, real parser/runner, library fallback,
# and the actual main.c refresh hooks with headless view stubs.
check-titles: build-native/title-check build-native/title-refresh-check build-native/title-bulk-check
	@./build-native/title-check
	@./build-native/title-refresh-check
	@./build-native/title-bulk-check

build-native/title-check: tools/title-check.c src/db.c src/library.c src/ssfetch.c src/ssrun.c src/rajson.c src/urlenc.c $(I18N_SRC) src/artsrc.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -DSS_TMP='"build-native/title-reply.json"' \
	      -o $@ tools/title-check.c src/db.c src/library.c src/ssfetch.c src/ssrun.c src/artsrc.c src/rajson.c src/urlenc.c $(I18N_SRC) -lsqlite3

build-native/title-bulk-check: tools/title-bulk-check.c src/artscrape.c src/db.c src/library.c src/ssfetch.c src/ssrun.c src/rajson.c src/urlenc.c $(I18N_SRC) src/artsrc.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc -DSS_BUSY_WAIT=0 -DSGDB_BUSY_WAIT=0 -DART_QUIET \
	      -DSS_TMP='"build-native/title-bulk-reply.json"' \
	      -DINDEX_TMP='"$(CURDIR)/build-native/title-bulk-check-artindex"' -DDAT_TMP='"$(CURDIR)/build-native/title-bulk-check-artdat"' \
	      -o $@ tools/title-bulk-check.c src/artscrape.c src/artsrc.c src/db.c src/library.c src/ssfetch.c src/ssrun.c src/rajson.c src/urlenc.c $(I18N_SRC) -lsqlite3

build-native/title-refresh-check: tools/title-refresh-check.c tools/title-refresh-hooks.py src/main.c src/library.c src/db.c src/stats.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	python3 tools/title-refresh-hooks.py src/main.c build-native/title-refresh-hooks.h
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Ibuild-native \
	      -o $@ tools/title-refresh-check.c src/library.c src/db.c src/stats.c $(I18N_SRC) -lsqlite3

# Host build of the launcher, for working on how the shelf looks.
native:
	$(MAKE) $(NATIVE_J) -f mk/native.mk VERSION=$(VERSION)

# The Pixel 2's card payload from this tree, against plastron's SDK (its make
# sdk), in Docker: build/px2/payload/ZyperOS, for tools/px2-deploy.sh or
# tools/qemu-run.sh deploy. tools/px2-payload.sh says how.
px2-payload:
	sh tools/px2-payload.sh

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
# src/zypermark.h). tools/genportmaster.py draws PortMaster's covers and icon
# (res/portmaster/), which check-pmart holds.
boot:
	python3 tools/genboot.py

# Two of those generators' art against what they draw now: tools/genloading.py's
# loading loop and loading.txt in res/loading/, and tools/genfirststart.py's
# first start's line and bar in res/boot/first-start/. Drawn again into a temp
# folder and compared, a PNG by its pixels (tools/gendrift.py). Both draw the
# same bytes run after run; the first start's line is text, and these pictures
# are what a Pillow with Raqm draws, the one PYIMG picks first. Not in CHECKS.
.PHONY: check-genloading check-genfirststart
check-genloading:
	@py='$(PYIMG)'; [ -n "$$py" ] || { echo "check-genloading: needs Pillow; set PYIMG" >&2; exit 1; }; \
	"$$py" -B tools/genloading.py --check

check-genfirststart:
	@py='$(PYIMG)'; [ -n "$$py" ] || { echo "check-genfirststart: needs Pillow; set PYIMG" >&2; exit 1; }; \
	"$$py" -B tools/genfirststart.py --check

# Fourteen config files became two databases, so properties that used to be
# obvious from looking at a text file now need asserting - chiefly that a
# shipped default never overwrites a choice, which is a bug this project
# already shipped once.
check-db: build-native/db-check
	@./build-native/db-check

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

build-native/savedlib-check: tools/savedlib-check.c src/savedlib.c src/savedlib.h src/library.c src/library.h \
                             src/db.c src/db.h src/atomic.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/savedlib-check.c src/savedlib.c src/library.c src/db.c src/atomic.c -lsqlite3 -lpthread

build-native/db-check: tools/db-check.c src/db.c src/db.h src/atomic.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/db-check.c src/db.c src/atomic.c -lsqlite3 -lpthread

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

build-native/gamern-check: tools/gamern-check.c src/gamern.c src/gamern.h src/gamedel.c src/gamedel.h \
                           src/esmedia.c src/esmedia.h src/library.c src/db.c src/favorites.c src/stats.c \
                           src/portinfo.c src/atomic.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -o $@ tools/gamern-check.c src/gamern.c src/gamedel.c src/esmedia.c src/library.c src/db.c \
	      src/favorites.c src/stats.c src/portinfo.c src/atomic.c $(I18N_SRC) -lsqlite3 -lpthread

# EmulationStation's folders and media (src/esmedia.h): covers, videos,
# details and the import on a card of small files in three ES layouts
check-esmedia: build-native/esmedia-check
	@./build-native/esmedia-check

build-native/esmedia-check: tools/esmedia-check.c src/esmedia.c src/esmedia.h src/library.c src/library.h \
                            src/db.c src/db.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g -Isrc \
	      -o $@ tools/esmedia-check.c src/esmedia.c src/library.c src/db.c -lsqlite3 -lpthread
	@if pkg-config --exists sdl2 SDL2_image SDL2_ttf 2>/dev/null; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@python3 tools/gamedel-e2e.py

build-native/gamedel-check: tools/gamedel-check.c src/gamedel.c src/gamedel.h src/portinfo.c src/portinfo.h src/library.c \
                            src/library.h src/db.c src/db.h src/favorites.c src/stats.c \
                            src/atomic.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/gamedel-check.c src/gamedel.c src/portinfo.c src/library.c src/db.c src/favorites.c \
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

build-native/storage-check: tools/storage-check.c src/storage.c src/storage.h src/gamedel.c src/portinfo.c \
                            src/gamedel.h src/library.c src/db.c src/favorites.c src/stats.c \
                            src/atomic.c $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/storage-check.c src/storage.c src/gamedel.c src/portinfo.c src/library.c src/db.c \
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

# Muse's view of the Music folder: the two shapes it reads, the litter a Mac
# leaves beside every track, and where an album's cover is kept.
check-muselib: build-native/muselib-check
	@./build-native/muselib-check

build-native/muselib-check: tools/muselib-check.c src/muselib.c src/muselib.h $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/muselib-check.c src/muselib.c $(I18N_SRC)

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

check-musequeue: build-native/musequeue-check
	@./build-native/musequeue-check

build-native/musequeue-check: tools/musequeue-check.c src/musequeue.c src/musequeue.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/musequeue-check.c src/musequeue.c

# Album covers from MusicBrainz: the rule that picks the record, against what
# MusicBrainz said about the first card's albums, and a run on stubbed network.
check-museart: build-native/museart-check
	@./build-native/museart-check

build-native/museart-check: tools/museart-check.c src/museart.c src/museart.h \
                            src/urlenc.c src/rajson.c tools/fixtures/musicbrainz/*.json $(I18N_SRC) FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/museart-check.c src/museart.c src/urlenc.c src/rajson.c $(I18N_SRC)

# src/zypermark.h is Zyper as triangles, generated from tools/zyperdef.py
# because C cannot import it; this proves it is current, and that the ground
# he is drawn on is Salt & Pepper's in tools/designdef.py. Part of `make
# check`, for the same reason payload.sh refuses a card whose systems.cfg names
# cores vendor/ does not have: the failure is silent otherwise, and it ships.
checkmark:
	python3 tools/checkmark.py

clean:
	rm -rf build build-native out

# Repeated frames through the real text cache: a line first drawn during a
# fade must reach the same full-opacity pixels as an uncached opaque line.
check-textfade:
	@if pkg-config --exists sdl2 SDL2_ttf SDL2_image; then \
		mkdir -p build-native; \
		$(CC) -std=gnu11 -D_GNU_SOURCE -O1 -g -Wall -Wextra \
		  $$(pkg-config --cflags sdl2 SDL2_ttf SDL2_image) -Isrc \
		  -o build-native/textfade-check tools/textfade-check.c src/ui.c src/roundrect.c src/theme.c src/look.c \
		  src/themefile.c src/linebreak.c src/blur.c src/cardgen.c src/sysmark.c $(I18N_SRC) \
		  $$(pkg-config --libs sdl2 SDL2_ttf SDL2_image) -lm && \
		ZYPEROS_HEADLESS=1 SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
		  ./build-native/textfade-check; \
	else echo "skipped: no SDL2 for the text fade check"; fi

# Tutorial drawing across frames: low-alpha first frames and a theme round-trip
# must not poison cached text, details or bars. No window or device required.
check-tour-colors:
	@if pkg-config --exists sdl2 SDL2_ttf SDL2_image; then \
		mkdir -p build-native; \
		$(CC) -std=gnu11 -D_GNU_SOURCE -O1 -g -Wall -Wextra \
		  $$(pkg-config --cflags sdl2 SDL2_ttf SDL2_image) -Isrc \
		  -o build-native/tour-colors-check tools/tour-colors-check.c src/ui.c src/roundrect.c \
		  src/theme.c src/themefile.c src/look.c src/linebreak.c src/blur.c src/cardgen.c \
		  src/sysmark.c src/tour.c src/tourdraw.c src/tourdevice.c src/zyperrig.c src/osdbar.c src/db.c $(I18N_SRC) \
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

build-native/shelfdir-check: tools/shelfdir-check.c src/shelfdir.c src/shelfdir.h src/library.c \
                             src/library.h src/db.c src/atomic.c FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/shelfdir-check.c src/shelfdir.c src/library.c src/db.c src/atomic.c \
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
check-favorites-shots: native
	@'$(PYIMG)' tools/favorites-shot-check.py

# The song's name in the top bar slides when too long, and Now Playing's foot
# is a row of hints (src/home.c, src/main.c)
.PHONY: check-np-footer
check-np-footer: native
	@'$(PYIMG)' tools/np-footer-check.py

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
# Boy game's, 534x480, a width of 1, odd ones - published as Muse publishes
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

# The card for a game or an album with no art, drawn on an art worker rather
# than on the frame: the frame's pixels byte for byte, every kind of title,
# both themes, a game's and an album's; a card of the theme that was never
# handed to the shelf after a turn; the fonts closed and opened again around
# a fallback game while cards are asked for. On the launcher's own objects.
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
