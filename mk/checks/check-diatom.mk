CHECKS += check-diatom
CHECKS_LINUX_SKIP += check-diatom
#| Diatom's own suite (diatom/Makefile's check) on its desktop port with the stub core: the port
#| seam, core facts, achievements, screens, the protocol, the state plane, resident cores, save
#| states packed and read back (old ones too), the card's frame of play written once a minute in
#| and never again; then the launcher's own Diatom link against it (tools/gamezone-check.c): the
#| zone chosen in Date & Time reaches a game's clock, a new zone the next game with no restart, and
#| a Diatom started again its first game (skips without SDL2)
