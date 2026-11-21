CHECKS += check-coreopts
#| a game's core options: the player's rows, then the device's defaults, then every device's, in
#| the library's order (fast forward's frameskip at each core's automatic value, PPSSPP's own
#| frameskip off on the Pixel 2: a late PSP frame is the frontend's to skip, from the PSP's
#| frameskip line in `config/systems.cfg`; the Saturn's rotating backgrounds kept on the CPU, as
#| the compute shader crashes the core); a row too long or with no tag refused whole; the one-time
#| cleanup of what 1.0.5 seeded, which keeps every row the player changed
