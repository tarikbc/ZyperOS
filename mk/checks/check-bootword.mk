CHECKS += check-bootword
#| the word in plastron's splash frames (the Pixel 2's and R36S's both themes, the Brick's, the
#| Smart Pro's) is its theme's, as `tools/zyperdef.py` colors it: Beard's Z in accent-ink, never
#| lime, and every letter 4.5:1 on its ground

check-bootword:
	@python3 -I tools/bootword-check.py
