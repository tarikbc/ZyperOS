CHECKS += check-gblink
#| the Link Cable's core side on this computer, with Gambatte and mk/patches/gambatte-link.patch
#| built for it (`tools/gblink-core.sh`): two cores over 127.0.0.1 (tools/gblink-loopback.c: bytes
#| whole both ways, a peer stopped 13 s costing one 10 s frame, the link back by itself, an unloaded
#| game freeing the other at once); then two of Diatom's desktop build, each told its side as the
#| launcher tells it (link_opt_lines' fourteen SETOPT lines over the paused game), running
#| tools/gblink-rom.py's test ROM, read off each one's screen (tools/gblink-diatom.py): bytes in
#| sequence both ways on the internal and the external clock, each side's count the other's, and a
#| menu open on one for 3 s no byte bad and the link going on. Then the real joiner: desk mode's
#| launcher (tools/gblink-desk.py standin, ZYPEROS_DESK_LINK_CABLE) opens the test ROM on Gambatte,
#| MENU, Link Cable, Join, against a stand-in host on 127.0.0.1: linked, and PASS on both screens.
#| Diatom built into diatom/build/gblink (skips without the core or SDL2)

GBLINK_CORE_FILE = $(firstword $(wildcard build-native/gblink/gambatte_libretro.dylib \
                                          build-native/gblink/gambatte_libretro.so))

check-gblink: native
	@if [ -z "$(GBLINK_CORE_FILE)" ]; then \
		echo "check-gblink: skipped, no Gambatte built for this computer (tools/gblink-core.sh)"; \
	elif ! pkg-config --exists sdl2 2>/dev/null && ! command -v sdl2-config > /dev/null; then \
		echo "check-gblink: skipped, no SDL2"; \
	else \
		mkdir -p build-native && \
		$(CC) -O2 -o build-native/gblink-loopback tools/gblink-loopback.c -Idiatom/src -ldl && \
		./build-native/gblink-loopback $(GBLINK_CORE_FILE) && \
		$(MAKE) --no-print-directory -C diatom $(NATIVE_J) BUILD=build/gblink all > /dev/null && \
		GBLINK_CORE=$(CURDIR)/$(GBLINK_CORE_FILE) python3 tools/gblink-diatom.py check && \
		python3 tools/gblink-desk.py standin --no-build; \
	fi
