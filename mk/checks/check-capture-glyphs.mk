CHECKS += check-capture-glyphs
#| the hotkey capture screen's glyphs after a long session (src/ui.c, button_font): each handheld's
#| SELECT and MENU capture with each shoulder, shot as a start's first frame and after the button
#| faces a session opens (`--glyph-warm`), the same picture and the shoulder's word in its pill
#| (the Pixel 2 test of 1.1.1-test.2 drew them as empty rings)

check-capture-glyphs: native
	@'$(PYIMG)' tools/capture-glyphs-check.py
