CHECKS += check-pico8-mp4
#| MP4 over your PICO-8 as over a Diatom game (src/mp4hand.h): the queue's file
#| (`/tmp/zyperos_mp4_queue`) written and read back whole, each kind of queue and a file of no
#| tracks, anything else refused both ways, under AddressSanitizer and UBSan where the compiler has
#| them, and your PICO-8 the one program MP4 plays on over (`port_keeps_mp4`); launch.sh's run_port
#| with stand-ins: a request with `mp4` fifth leaves MP4 playing, starts the follower (`zyperos.elf
#| --mp4-follow`) beside the program, gives portkeys `--follow` with its pid and ends it before the
#| launcher comes back, and OpenBOR's request stops MP4 as before; then the real launcher in desk
#| mode's dummy window against a stand-in daemon (`ZYPEROS_MP4_SOCKET`): back from PICO-8 the queue
#| taken on with no PLAY and no resume, a cart opened writing the request and the file, the
#| follower's next track at an END (its log lines `port: ` ones, which splash leaves off the
#| loading screen), and SELECT in the in-game menu over PICO-8 (the follower stopped) opening Now
#| Playing, L1 there, SELECT let go closing it, B Continue, the file written back and the follower
#| going on from it; over a program with no file SELECT opens nothing; PICO-8 quiet while MP4
#| plays: the equalizer's file's cut (`ZYPEROS_EQ_FILE`) set as the launcher hands over with MP4
#| playing, cleared at a pause and set again by the follower within 0.5 s, written by the in-game
#| menu as it closes, cleared as the follower ends and as the launcher starts, and eq-port.sh's
#| PCMs byte for byte every port's, no softvol stage; a USB-C DAC in (`/proc/asound/card1/usbid`):
#| your PICO-8 into the system's `usb` PCM once `zyperos.elf --alsa-try` opens it, the mix as
#| before when it does not, with no such PCM, with no DAC, and for every other port; and
#| `--alsa-try` against this computer's alsa-lib on Linux, a chain that opens and one that does
#| not. Every wait is for an event (the stand-ins beside the program up, a screen drawn, a key
#| taken and the frame after it drawn, the daemon's song drawn), never a time, and a string drawn
#| is found wherever its line begins (ALSA's lines on Linux)
