CHECKS += check-link-core
#| the Link Cable on a Game Boy game running on mGBA and a game's own core (DESIGN.md 11.72, Tarik's
#| decision of 2026-11-27), in desk mode on a card of its own (tools/link-core-check.py): the row on
#| mgba, A asking Restart on Gambatte with Cancel first and B the same, Restart starting the game
#| again on gambatte with its page open at once, core.GB.<file> gambatte and the shelf's core
#| untouched, another Game Boy game still on mgba, the first on its own gambatte, its details
#| page's Core row stepping back to the shelf's (the key gone) and the game on mgba again. Skips
#| without Gambatte built for this computer (tools/gblink-core.sh)

check-link-core: native
	@if [ -z "$(GBLINK_CORE_FILE)" ]; then \
		echo "check-link-core: skipped, no Gambatte built for this computer (tools/gblink-core.sh)"; \
	elif ! pkg-config --exists sdl2 2>/dev/null && ! command -v sdl2-config > /dev/null; then \
		echo "check-link-core: skipped, no SDL2"; \
	else \
		$(MAKE) --no-print-directory -C diatom $(NATIVE_J) BUILD=build/linkcore all > /dev/null && \
		python3 tools/link-core-check.py; \
	fi
