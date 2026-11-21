CHECKS += check-displayback
#| the display taken back after a game (src/displayback.h), against a stand-in display and clock
#| with Linux's request numbers: a clean end (master at once, the game's last frame left up), a
#| crash (master asked for again until the dying Diatom lets go, then a black picture put up on the
#| CRTC it left empty, in SDL's own format so SDL's next flip is allowed, kept for the next crash),
#| a crash that turned the CRTC off (the panel's preferred mode), QEMU's XRGB8888 frames, a panel
#| listing more modes than it holds, a Diatom that never lets go (refused on the bound), the
#| per-frame try, and a refusal without master
