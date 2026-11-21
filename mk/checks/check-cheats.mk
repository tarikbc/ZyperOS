CHECKS += check-cheats
#| A game's cheats with no device (src/cht.h, src/cheats.h): the .cht reader (RetroArch's keys
#| quoted or not, CRLF, a byte-order mark, keys out of order, counts too small and too big,
#| RetroArch's own memory pokes and cheats with no code left out, a long description cut at a
#| character, a text that is no .cht) and the list on a card made of small files (the player's own
#| files first as one group, under NextUI's names too, a game in a subfolder, libretro's files each
#| a group named for its code type, why a game has none, which are on kept as ids that survive a
#| reload and an update, the codes Diatom is handed, the on-first order)
