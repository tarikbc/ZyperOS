CHECKS += check-gamedel
#| Delete Game on a scratch card holding a cartridge, a multi-disc set in its folder, a `.cue` with
#| its tracks and an arcade set beside `neogeo.zip`, each with saves, states, a cover, a video and
#| a set: exactly the game's files go, never a neighbour's, never `neogeo.zip`, never a file
#| another game shares; its rows in the library database go and the neighbours' stay; a sheet or a
#| folder that leads out of its folders is refused with nothing touched. Then the real launcher,
#| headless (`--delete-now`): the shelf after, in place, on the next game or the one before, from
#| Favorites, down to the empty home (builds the host launcher; that part skips without SDL2)
