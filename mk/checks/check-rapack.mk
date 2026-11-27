CHECKS += check-rapack
#| a set's achievement badges in one file (`src/rapack.h`), on a card in /tmp: the pack beside the
#| set, its loose PNGs taken in, read back the same and only then deleted, a link's text and another
#| set's badge left alone, a second fetch added beside the first and a loose one in place of a packed
#| one, nothing loose leaving the pack unwritten, a cut pack read as none, a pack not written
#| deleting nothing

check-rapack: build-native/rapack-check
	@./build-native/rapack-check

build-native/rapack-check: tools/rapack-check.c src/rapack.c src/rapack.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/rapack-check.c src/rapack.c -lpthread
