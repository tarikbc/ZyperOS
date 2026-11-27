CHECKS += check-link
#| the Link Cable's launcher side (`src/link.c`), on fakes for every process, network and protocol
#| action: offered on a shelf with Gambatte (the Game Boys', on mgba too) and only on a device that
#| has it; the adapter's last two bytes as the network's name (`ZyperOS-Link-7F2A`) and the
#| handheld's (`Pixel 2 7F2A`), with no adapter (the cable) the serial's last four digits (`Pixel 2
#| 1C2D`, never 0000), a scan's names read back to hosts (not File Transfer's, a home's or its own);
#| the host's code four digits from the kernel's random (the real maker's 20000 draws even), new
#| each time it hosts under the same name, the WPA2 password made from it alone, a typed code read
#| as four digits or refused, and a wrong one joining nothing; an address as Gambatte's twelve
#| digits, and nothing that is not one; the fourteen SETOPT lines for the server and the client and
#| the one for off; the datagrams read back, tabs flattened, anything else none; the host's network
#| before its socket, nothing to Diatom before a hello, the server's options, the welcome and one
#| notice on it, a beat every half second, a stranger told busy, six seconds with no beat a lost
#| link (said once, the core's link off); the joiner's scan again while it is looked at, its join
#| with the typed code, no hello before an address, its socket on that address and 56399 (where the
#| host answers), and one every half second until the welcome, a welcome or busy from any address
#| but the host's ignored, then the client's options; once linked, nothing from a third address
#| heard; a bye said as left; each failure (no hotspot, no scan, no join, no answer, busy, Diatom
#| refusing); the game's end quiet; the beat's socket on 10.42.1.1 alone; the stop's bye twice with
#| nothing to Diatom; the USB-C cable (the adapter's Wi-Fi before it, no code, no hotspot, the
#| gadget and the beat on 10.42.0.1, the joiner's port the host and 10.42.0.2, the client's address
#| 10.42.0.1, no hotspot to lose, each failure); and `src/linknet.c`'s socket on the loopback

check-link: build-native/link-check
	@./build-native/link-check

build-native/link-check: tools/link-check.c src/link.c src/link.h src/linknet.c src/linknet.h \
                         src/hotspot.h FORCE
	@mkdir -p build-native
	$(CC) -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -O1 -g \
	      -o $@ tools/link-check.c src/link.c src/linknet.c
