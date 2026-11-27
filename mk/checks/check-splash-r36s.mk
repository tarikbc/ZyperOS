CHECKS += check-splash-r36s
#| the R36S's boot pictures (`plastron/buildroot/package/splash/r36s/`) are the Pixel 2's of now,
#| turned back to 640x480: none stale since a Pixel 2 picture changed, none changed or missing

# scripts/make-splash-frames.py --r36s turns them; its SOURCES file has the
# sha256 of each Pixel 2 file they came from, and of each of them
check-splash-r36s:
	@python3 -I plastron/scripts/make-splash-frames.py --r36s-check
