CHECKS += check-third-party-rows
#| each handheld's package list (plastron's `board/<board>/third-party-packages.md`) has a row for every
#| package its defconfig turns on that any board's list names, before a release build refuses it

check-third-party-rows:
	@python3 tools/third-party-rows-check.py
