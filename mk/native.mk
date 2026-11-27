# Native dev build (macOS/Linux host): make -f mk/native.mk
#
# A window run is desk mode (src/device/desk.h): tools/desk runs it on a
# sample card, as the Pixel 2 or the Brick, with the keyboard, the mouse and a
# game controller as the handheld's buttons (docs/building.md, "Work on the UI
# without a device"). A headless run (ZYPEROS_HEADLESS) is the shots' and the
# checks'.
BUILD := build-native
# The Brick's device file, whose hardware is behind __linux__ with a host stub
# beside it: that is what this build has always run.
DEVICE ?= brick
# The string tables are generated, not committed (Makefile, I18N_SRC), so
# they are named on their own: the wildcard is read before they exist. Their
# rule is below the first target.
I18N_TABLES := src/i18n_tables.c
# src/ds/: the design system's components and marks (DESIGN.md, "Where each part lives")
# Every device's profile (src/device/profile.h), so --device can wear any;
# the host's own is desktop's
SRC := $(I18N_TABLES) $(filter-out $(I18N_TABLES),$(wildcard src/*.c)) $(wildcard src/ds/*.c) src/device/$(DEVICE).c $(wildcard src/device/$(DEVICE)_hw.c) \
       $(wildcard src/device/*_profile.c) \
       src/device/td_model.c $(filter-out src/device/td_model.c,$(wildcard src/device/*_model.c)) \
       src/device/pixel2_coreopts.c $(filter-out src/device/pixel2_coreopts.c,$(wildcard src/device/*_coreopts.c))
# Desk mode (src/device/desk.h): the host build's window as a handheld. Here
# alone: no device builds it.
SRC += src/device/desk.c src/device/deskface.c src/device/deskcable.c
# src/main.c and the files split from it, which call into one another: the
# checks that link the rest of src/ leave them all out.
MAIN_SRC := src/main.c src/about_screen.c src/aout_route.c src/bgm_screen.c \
            src/bt_screen.c src/cheats_screen.c src/clock_screen.c src/ctl_channel.c src/ctltab_screen.c \
            src/devmode_screen.c src/dl_screen.c src/dlcount.c src/gamedel_screen.c src/gameinfo_screen.c \
            src/gamern_screen.c src/hotkeys_screen.c src/hover_sweep.c src/hudmenu_screen.c src/ingame_screen.c src/launch_screen.c \
            src/lights_screen.c src/media_screen.c src/menu_panel.c src/menu_runner.c src/menukeys_screen.c \
            src/mp4_screen.c src/nowplaying_looks.c src/nowplaying_screen.c src/place.c src/player2_screen.c src/link_screen.c \
            src/portload.c src/powerscreens.c src/radl_screen.c src/raqueue.c src/saver_screen.c \
            src/scraper_screen.c src/search_screen.c src/shelf_draw.c src/shelf_keys.c \
            src/shelfbuild.c src/shots.c src/shots_screen.c src/signin_screen.c src/stats_screen.c \
            src/storage_screen.c src/sysmenu_screen.c src/homesys_screen.c src/collect_screen.c src/tour_screen.c \
            src/update_screen.c src/usbdrive_screen.c src/xfer_screen.c src/zyperdraw.c

PKGS := sdl2 SDL2_image SDL2_ttf
VERSION ?= 0.0
# A different VERSION from the last build's deletes the binary and its
# objects (main.c and net.c are built with it). See Makefile.
ifneq ($(VERSION),$(shell cat $(BUILD)/version 2>/dev/null))
$(shell mkdir -p $(BUILD) && rm -rf $(BUILD)/zyperos $(BUILD)/obj && echo '$(VERSION)' > $(BUILD)/version)
endif
CFLAGS := -DZYPEROS_VERSION='"$(VERSION)"' -O1 -g -Wall -Wextra -Wno-unused-parameter -std=gnu11 -D_GNU_SOURCE \
          $(shell pkg-config --cflags $(PKGS))
# liblzma for a CHD's LZMA hunks (src/chd.c); Homebrew's xz on a Mac
CFLAGS += $(shell pkg-config --cflags liblzma 2>/dev/null)
LDLIBS := $(shell pkg-config --libs $(PKGS)) -lsqlite3 -lz \
          $(shell pkg-config --libs liblzma 2>/dev/null || echo -llzma) -lpthread -lm
# ALSA only on Linux: src/btvol.c is a no-op without it, on the Mac
ifeq ($(shell uname -s),Linux)
  LDLIBS += -lasound
endif
# Desk mode's real-size zoom asks the display its millimetres (desk.c)
ifeq ($(shell uname -s),Darwin)
  LDLIBS += -framework CoreGraphics
endif

# The same generated header the device build uses, for the same reason, so the
# host build can talk to ScreenScraper too. See mk/cross.mk.
CREDS := $(BUILD)/ss_creds.h
CFLAGS += -I$(BUILD)
# src too, so a device file in src/device/ includes the shared headers by name
CFLAGS += -Isrc
# The pseudo-locale, xx (src/i18n.h): every string accented and longer, for
# the host's shots to find what does not fit. Never on a device.
CFLAGS += -DI18N_PSEUDO
# The profile the host wears unless told (--device), and all of them linked
CFLAGS += -DZYPEROS_PROFILE=device_profile_desktop -DZYPEROS_PROFILES_ALL
CFLAGS += -DZYPEROS_DESK

.PHONY: all creds
all: $(BUILD)/zyperos
creds: $(CREDS)

# tools/i18n.py writes the string tables whenever res/strings or the script
# is newer, so a tree fresh from git builds (Makefile, I18N_SRC)
$(I18N_TABLES): $(wildcard res/strings/*.txt) tools/i18n.py
	@python3 tools/i18n.py

# Written every build and moved into place only when it changed, so the
# objects that include it are rebuilt only then. A name of its own for each
# writer, because several checks can ask for it at once (make check -j).
$(CREDS): FORCE
	@mkdir -p $(BUILD)
	@umask 077; { \
	  echo '/* Generated by the build from the environment. See src/ss.h. */'; \
	  printf '#define SS_DEVID   "%s"\n' "$$(printf '%s' "$$SS_DEVID"   | sed 's/[\\"]/\\\\&/g')"; \
	  printf '#define SS_DEVPASS "%s"\n' "$$(printf '%s' "$$SS_DEVPASS" | sed 's/[\\"]/\\\\&/g')"; \
	} > $@.new.$$$$ && \
	{ cmp -s $@.new.$$$$ $@ 2>/dev/null || mv -f $@.new.$$$$ $@; }; \
	rm -f $@.new.$$$$

# Rules that must run at every build depend on this.
.PHONY: FORCE
FORCE:

# One object a source file, so an edit rebuilds that file alone and `make -j`
# builds the rest at once (the Makefile's native target asks for -j). Every
# header is a prerequisite of every object: the coarse rule, and the safe one.
OBJ := $(BUILD)/obj
OBJS := $(patsubst src/%.c,$(OBJ)/%.o,$(SRC))
HDRS := $(wildcard src/*.h) $(wildcard src/ds/*.h) $(wildcard src/device/*.h) $(wildcard src/third_party/*.h)

$(OBJ)/%.o: src/%.c $(HDRS) $(CREDS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/zyperos: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

# Focused home behavior and renderer checks use the real production modules:
# the launcher's own objects, all but main.c's and the files split out of it
# (MAIN_SRC), which need main.c's own symbols.
CHECK_OBJS := $(filter-out $(patsubst src/%.c,$(OBJ)/%.o,$(MAIN_SRC)),$(OBJS))

$(BUILD)/home-check: tools/home-check.c $(CHECK_OBJS) FORCE
	$(CC) $(CFLAGS) -o $@ tools/home-check.c $(CHECK_OBJS) $(LDLIBS)

$(BUILD)/games-check: tools/games-check.c $(CHECK_OBJS) FORCE
	$(CC) $(CFLAGS) -o $@ tools/games-check.c $(CHECK_OBJS) $(LDLIBS)

$(BUILD)/shaderpic-check: tools/shaderpic-check.c $(CHECK_OBJS) FORCE
	$(CC) $(CFLAGS) -o $@ tools/shaderpic-check.c $(CHECK_OBJS) $(LDLIBS)

# The card with no art drawn on the art workers, against the frame's own
# (tools/cardpaint-check.c), and finished pictures handed to the frame a few
# a frame (tools/putpace-check.c): both need the workers, the fonts and a
# renderer, so the launcher's own objects.
$(BUILD)/cardpaint-check: tools/cardpaint-check.c $(CHECK_OBJS) FORCE
	$(CC) $(CFLAGS) -o $@ tools/cardpaint-check.c $(CHECK_OBJS) $(LDLIBS)

# A video snap of any shape read and drawn (tools/snapsize-check.c), with
# src/snapview.c built here against a frames file of the check's own, under
# UBSan where the compiler has it
SNAP_SAN := $(shell echo 'int main(void){return 0;}' | $(CC) -fsanitize=undefined -x c - -o /dev/null 2>/dev/null && echo -fsanitize=undefined -fno-sanitize-recover=all)
$(BUILD)/snapsize-check: tools/snapsize-check.c src/snapview.c $(filter-out $(OBJ)/snapview.o,$(CHECK_OBJS)) FORCE
	$(CC) $(CFLAGS) $(SNAP_SAN) -DSNAP_FRAMES='"$(CURDIR)/$(BUILD)/snapsize-frames"' -o $@ \
	      tools/snapsize-check.c src/snapview.c $(filter-out $(OBJ)/snapview.o,$(CHECK_OBJS)) $(LDLIBS)

$(BUILD)/putpace-check: tools/putpace-check.c $(CHECK_OBJS) FORCE
	$(CC) $(CFLAGS) -o $@ tools/putpace-check.c $(CHECK_OBJS) $(LDLIBS)

# A measurement, not a check: the frame thread's time while a shelf lands
# (tools/landbench.c, make landbench).
$(BUILD)/landbench: tools/landbench.c $(CHECK_OBJS) FORCE
	$(CC) $(CFLAGS) -o $@ tools/landbench.c $(CHECK_OBJS) $(LDLIBS)
