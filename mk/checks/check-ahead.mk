CHECKS += check-ahead
#| MP4's sound decoded ahead (`src/mp4d/ahead.h`) against a stand-in output in real time: every
#| frame once and in order through the ring's wrap, a push waiting while it is full, a decoder slow
#| one chunk in five leaving the output never dry where writing straight to it ran dry, a hold
#| throwing the ring away, a failed output refusing the push until a hold

check-ahead: build-native/ahead-check
	@./build-native/ahead-check

build-native/ahead-check: tools/ahead-check.c src/mp4d/ahead.c src/mp4d/ahead.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O2 -g \
	      -o $@ tools/ahead-check.c src/mp4d/ahead.c -lpthread
