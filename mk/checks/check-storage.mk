CHECKS += check-storage
CHECKS_LINUX_SKIP += check-storage
#| MENU > Storage's numbers on a scratch card: each part, each game's bytes (what Delete Game gives
#| back) and the bar held to the clusters the files take; a second start reads the cache and lists
#| no folder again, a game added lists its own folder alone, a cache it cannot read is measured
#| again; the walk on its thread ends and stops when asked; Unused Art and Old Save States delete
#| exactly their files. Then the real launcher, headless (`--storage delete-open`): Delete Game's
#| question opens on the page from nothing, never as a swap from the panel before it (builds the
#| host launcher; that part skips without SDL2)
