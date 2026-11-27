CHECKS += check-mute
# Not on Linux: there the host build takes the Brick's own hardware file
# (src/device/brick.c under __linux__), which reads the switch, not
# ZYPEROS_MUTE_SWITCH
CHECKS_LINUX_SKIP += check-mute
#| the Brick's mute switch (#482) on the host, forced down and up (`ZYPEROS_MUTE_SWITCH=on`,
#| `--device brick`), both themes: the log says "brick: mute switch on" or "off" once, the top
#| bar's corner has the crossed speaker only muted, and the volume bar writes Muted where its steps
#| were; a profile with no switch says and shows nothing (builds the host launcher; skips without
#| SDL2)
check-mute:
	@if pkg-config --exists sdl2 SDL2_ttf SDL2_image; then \
		$(MAKE) --no-print-directory native || exit 1; \
	fi
	@python3 tools/mute-shot-check.py
