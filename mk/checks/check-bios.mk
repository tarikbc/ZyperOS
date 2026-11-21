CHECKS += check-bios
#| a shelf's BIOS as its core judges it: every disc bios name in systems.cfg has a size, and so has
#| every name a type of file needs (a bios line with extensions: an .fds image needs disksys.rom, a
#| cartridge beside it nothing), and a 0-byte, short or long file in Bios/ is refused and named in
#| the core's order (with the one delete that fixes it when a good later name is there), while a
#| right-size one starts
